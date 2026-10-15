#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "ycnet/ycnet.hpp"

namespace {
struct numbered_packet {
    static constexpr int packet_id = 92;
    static constexpr bool is_reliable = true;
    std::uint32_t number;
};

int failures = 0;
void check(const bool ok, const char* name) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!ok) ++failures;
}

void inject_ack(ycnet::rudp_handler& sender, packet_ack_type ack) {
    sender.on_receive(ack.data(), ack.size());
}

struct link {
    std::vector<std::vector<char>> data;
    std::vector<std::vector<char>> acks;
    std::vector<std::uint32_t> delivered;
    std::int64_t now{};
    ycnet::rudp_handler sender;
    ycnet::rudp_handler receiver;

    link()
        : sender([](const std::string&) {},
            [&](char* p, std::size_t n) { data.emplace_back(p, p + n); }, [&] { return now; }, ycnet::session_binding{101, 202}),
          receiver([](const std::string&) {},
            [&](char* p, std::size_t n) { acks.emplace_back(p, p + n); }, [&] { return now; }, ycnet::session_binding{202, 101}) {
        packet_events[numbered_packet::packet_id] = [&](void* body, packet_size_type, std::size_t) {
            numbered_packet packet{};
            std::memcpy(&packet, body, sizeof(packet));
            delivered.push_back(packet.number);
        };
    }
    ~link() { packet_events[numbered_packet::packet_id] = {}; }

    bool send(const std::uint32_t number) {
        numbered_packet packet{number};
        return sender.send(packet);
    }
    void deliver_data() {
        auto pending = std::move(data);
        data.clear();
        for (auto& packet : pending) receiver.on_receive(packet.data(), packet.size());
    }
    void deliver_acks() {
        auto pending = std::move(acks);
        acks.clear();
        for (auto& packet : pending) sender.on_receive(packet.data(), packet.size());
    }
    void roundtrip(const std::uint32_t number) {
        if (!send(number)) { ++failures; return; }
        deliver_data();
        deliver_acks();
        ++now;
    }
};

void test_old_data_and_ack_after_slot_reuse() {
    link wire;
    wire.send(0);
    const auto old_data = wire.data.front();
    wire.deliver_data();
    const auto old_ack = wire.acks.front();
    wire.deliver_acks();
    for (std::uint32_t i = 1; i < 64; ++i) wire.roundtrip(i);

    auto replay = old_data;
    wire.receiver.on_receive(replay.data(), replay.size());
    check(wire.delivered.size() == 64, "old data is not delivered after slot reuse");
    wire.deliver_acks();

    wire.send(64); // Keep the new packet undelivered.
    const auto before = wire.sender.metrics().acks_received;
    replay = old_ack;
    wire.sender.on_receive(replay.data(), replay.size());
    check(wire.sender.metrics().acks_received == before,
          "old ACK cannot complete a new packet in the same slot");
    wire.deliver_data();
    wire.deliver_acks();
    check(wire.delivered.size() == 65 && wire.delivered.back() == 64
              && wire.sender.metrics().acks_received == 65,
          "new packet is delivered and acknowledged after rejecting old traffic");

    for (std::uint32_t i = 65; i < 4096; ++i) wire.roundtrip(i);
    replay = old_data;
    wire.receiver.on_receive(replay.data(), replay.size());
    check(wire.delivered.size() == 4096, "old data stays rejected over many buffer cycles");
    bool ordered = wire.delivered.size() == 4096;
    for (std::size_t i = 0; i < wire.delivered.size(); ++i)
        ordered = ordered && wire.delivered[i] == i;
    check(ordered, "4096 payloads retain their full message identity and order");
}

void test_window_is_anchored_to_oldest_unacked() {
    link wire;
    bool accepted = true;
    for (std::uint32_t i = 0; i < 64; ++i) accepted = wire.send(i) && accepted;
    // Free later slots but deliberately leave sequence 0 unacknowledged.
    for (int i = 1; i < 64; ++i) inject_ack(wire.sender, ycnet::make_ack_packet(i, {202, 101}));
    check(accepted && !wire.send(64), "oldest unacknowledged packet anchors the window");
    inject_ack(wire.sender, ycnet::make_ack_packet(0, {202, 101}));
    check(wire.send(64), "window advances when its oldest packet is acknowledged");
}

void test_reorder_and_receive_window_across_slots() {
    link wire;
    for (std::uint32_t i = 0; i < 63; ++i) wire.roundtrip(i);
    wire.send(63);
    wire.send(64);
    auto later = wire.data[1];
    wire.receiver.on_receive(later.data(), later.size());
    check(wire.delivered.size() == 63, "sequence 64 waits for 63 even though its slot is zero");

    auto outside = later;
    ycnet::packet::udp::ack_header header(127);
    header.source_session = 101;
    header.destination_session = 202;
    const auto raw = header.to_ack();
    std::copy(raw.begin(), raw.end(), outside.begin());
    wire.receiver.on_receive(outside.data(), outside.size());
    check(wire.acks.empty(), "packet beyond receive window is neither delivered nor acknowledged");

    auto missing = wire.data[0];
    wire.data.clear();
    wire.receiver.on_receive(missing.data(), missing.size());
    wire.deliver_acks();
    check(wire.delivered.size() == 65 && wire.delivered[63] == 63 && wire.delivered[64] == 64,
          "out-of-window packet cannot overwrite the missing sequence slot");
}

void test_full_reordered_window() {
    link wire;
    for (std::uint32_t i = 0; i < 64; ++i) wire.send(i);
    for (auto i = wire.data.rbegin(); i != wire.data.rend(); ++i)
        wire.receiver.on_receive(i->data(), i->size());
    wire.deliver_acks();
    bool ok = wire.delivered.size() == 64 && wire.sender.metrics().acks_received == 64;
    for (std::size_t i = 0; i < wire.delivered.size(); ++i) ok = ok && wire.delivered[i] == i;
    check(ok, "a full reversed 64-packet window drains without losing its end cursor");
}

void test_sequence_limit_does_not_reuse_zero() {
    ycnet::rudp::rudp_buffers buffers(1);
    std::vector<int> resend;
    char body = 'X';
    const int slot = ycnet::rudp::ready_to_send(
        buffers.send_buffer, resend, &body, 1, true, max_packet_sequence, 100);
    check(slot == 63 && ycnet::rudp::get_seq(buffers.send_buffer[slot].data) == max_packet_sequence,
          "maximum uint32 sequence retains all bits on the wire");
    ycnet::rudp::rto_state rto;
    check(ycnet::rudp::set_send_complete(buffers.send_buffer, resend, rto, 63, 110) == -1,
          "a slot-number ACK cannot complete the maximum full sequence");
    check(ycnet::rudp::set_send_complete(buffers.send_buffer, resend, rto, max_packet_sequence, 110) == slot,
          "maximum full sequence is acknowledged by its full identity");
    check(ycnet::rudp::ready_to_send(buffers.send_buffer, resend, &body, 1, true,
              ycnet::rudp::get_next_seq(max_packet_sequence), 120) == -1,
          "wire sequence exhaustion refuses send instead of reusing zero");
}
} // namespace

int main() {
    test_old_data_and_ack_after_slot_reuse();
    test_window_is_anchored_to_oldest_unacked();
    test_reorder_and_receive_window_across_slots();
    test_full_reordered_window();
    test_sequence_limit_does_not_reuse_zero();
    return failures ? 1 : 0;
}

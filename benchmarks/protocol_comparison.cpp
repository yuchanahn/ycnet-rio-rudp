#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "ycnet/ycnet.hpp"
extern "C" {
#include "ikcp.h"
}

namespace ycnet::benchmark {
constexpr int update_interval_ms = 10;
constexpr int application_interval_ms = 10;
constexpr int max_inflight = 16;

struct network {
    struct datagram {
        int due_ms;
        bool to_receiver;
        std::vector<char> bytes;
    };
    int now_ms{};
    int one_way_ms;
    int loss_percent;
    std::mt19937 random;
    std::deque<datagram> queue;
    std::uint64_t datagrams{};
    std::uint64_t attempted_bytes{};
    std::uint64_t dropped{};

    void send(const char* data, const std::size_t size, const bool to_receiver) {
        ++datagrams;
        attempted_bytes += size;
        if (random() % 10000 < static_cast<unsigned>(loss_percent * 100)) {
            ++dropped;
            return;
        }
        queue.push_back({now_ms + one_way_ms, to_receiver, {data, data + size}});
    }

    template <typename receive, typename acknowledge>
    void pump(receive&& receiver, acknowledge&& sender) {
        while (!queue.empty() && queue.front().due_ms <= now_ms) {
            auto packet = std::move(queue.front());
            queue.pop_front();
            if (packet.to_receiver) receiver(packet.bytes);
            else sender(packet.bytes);
        }
    }
};

template <std::size_t payload_size>
struct message {
    static constexpr int packet_id = 90;
    static constexpr bool is_reliable = true;
    std::uint32_t index{};
    std::array<char, payload_size - sizeof(index)> body{};
};

template <std::size_t payload_size>
message<payload_size> make_message(const int index) {
    static_assert(sizeof(message<payload_size>) == payload_size);
    message<payload_size> packet{};
    packet.index = static_cast<std::uint32_t>(index);
    for (std::size_t i = 0; i < packet.body.size(); ++i)
        packet.body[i] = static_cast<char>((index + static_cast<int>(i)) % 127);
    return packet;
}

struct result {
    std::string protocol;
    int payload_bytes;
    int rtt_ms;
    int loss_percent;
    std::uint32_t seed;
    int offered;
    int accepted{};
    int delivered{};
    int finished_ms{};
    int peak_backlog{};
    bool valid{true};
    std::vector<int> end_to_end_latency;
    std::vector<int> transport_latency;
    std::uint64_t datagrams{};
    std::uint64_t wire_bytes{};
    std::uint64_t dropped{};
    int final_rto_ms{};
    bool connection_timed_out{};
    int pending_at_finish{};
    bool deadline_exceeded{};
};

template <std::size_t payload_size>
void record(result& output, const char* data, const std::size_t size,
            const int now, const std::vector<int>& admitted) {
    if (size != payload_size) {
        output.valid = false;
        return;
    }
    message<payload_size> packet{};
    std::memcpy(&packet, data, size);
    const auto expected = make_message<payload_size>(output.delivered);
    if (packet.index != expected.index || packet.body != expected.body
        || packet.index >= admitted.size() || admitted[packet.index] < 0) {
        std::cerr << "invalid_delivery protocol=" << output.protocol << " rtt=" << output.rtt_ms
                  << " loss=" << output.loss_percent << " seed=" << output.seed
                  << " expected_index=" << output.delivered << " actual_index=" << packet.index
                  << " payload_matches=" << (packet.body == expected.body) << '\n';
        output.valid = false;
        return;
    }
    output.end_to_end_latency.push_back(now - static_cast<int>(packet.index) * application_interval_ms);
    output.transport_latency.push_back(now - admitted[packet.index]);
    ++output.delivered;
}

void finish(result& output, const network& link) {
    output.finished_ms = link.now_ms;
    output.datagrams = link.datagrams;
    output.wire_bytes = link.attempted_bytes;
    output.dropped = link.dropped;
}

template <std::size_t payload_size>
result run_ycnet(const int messages, const int rtt, const int loss, const std::uint32_t seed) {
    network link{.one_way_ms = rtt / 2, .loss_percent = loss, .random = std::mt19937(seed)};
    result output{.protocol = "ycnet", .payload_bytes = payload_size,
                  .rtt_ms = rtt, .loss_percent = loss, .seed = seed, .offered = messages};
    std::vector<int> admitted(messages, -1);
    rudp_handler sender([](const std::string&) {},
        [&](char* data, const std::size_t size) { link.send(data, size, true); },
        [&]() -> int64_t { return link.now_ms; }, session_binding{101, 202});
    rudp_handler receiver([](const std::string&) {},
        [&](char* data, const std::size_t size) { link.send(data, size, false); },
        [&]() -> int64_t { return link.now_ms; }, session_binding{202, 101});
    packet_events[message<payload_size>::packet_id] =
        [&](void* data, const packet_size_type size, const std::size_t) {
            // YCNet dispatch length includes its 3-byte size/id header.
            record<payload_size>(output, static_cast<const char*>(data),
                                 size - packet::header_size, link.now_ms, admitted);
        };
    const int deadline = messages * application_interval_ms + 30000;
    for (; link.now_ms <= deadline && output.valid; ++link.now_ms) {
        const int due = (std::min)(messages, link.now_ms / application_interval_ms + 1);
        while (output.accepted < due
               && output.accepted - static_cast<int>(sender.metrics().acks_received) < max_inflight) {
            auto packet = make_message<payload_size>(output.accepted);
            const auto status = sender.try_send(packet);
            if (status == send_result::would_block) break;
            if (status != send_result::accepted) break;
            admitted[output.accepted++] = link.now_ms;
        }
        output.peak_backlog = (std::max)(output.peak_backlog, due - output.accepted);
        if (link.now_ms % update_interval_ms == 0) sender.process_retransmissions();
        link.pump(
            [&](std::vector<char>& bytes) { receiver.on_receive(bytes.data(), bytes.size()); },
            [&](std::vector<char>& bytes) { sender.on_receive(bytes.data(), bytes.size()); });
        if (sender.state() == connection_state::closed) break;
        if (output.delivered == messages && sender.metrics().pending_reliable == 0) break;
    }
    // Avoid retaining references to this run in the global packet registry.
    packet_events[message<payload_size>::packet_id] = {};
    output.final_rto_ms = sender.metrics().current_rto_ms;
    output.connection_timed_out = sender.metrics().connection_timed_out;
    output.pending_at_finish = static_cast<int>(sender.metrics().reliable_sent - sender.metrics().acks_received);
    output.deadline_exceeded = link.now_ms > deadline;
    finish(output, link);
    return output;
}

struct kcp_output_context {
    network* link;
    bool to_receiver;
};

int kcp_output(const char* data, const int size, ikcpcb*, void* user) {
    auto& context = *static_cast<kcp_output_context*>(user);
    context.link->send(data, size, context.to_receiver);
    return 0;
}

using kcp_pointer = std::unique_ptr<ikcpcb, decltype(&ikcp_release)>;

template <std::size_t payload_size>
result run_kcp(const int messages, const int rtt, const int loss,
               const std::uint32_t seed, const bool fast) {
    network link{.one_way_ms = rtt / 2, .loss_percent = loss, .random = std::mt19937(seed)};
    result output{.protocol = fast ? "kcp_fast" : "kcp_normal", .payload_bytes = payload_size,
                  .rtt_ms = rtt, .loss_percent = loss, .seed = seed, .offered = messages};
    std::vector<int> admitted(messages, -1);
    kcp_output_context send_context{&link, true}, ack_context{&link, false};
    kcp_pointer sender(ikcp_create(1, &send_context), &ikcp_release);
    kcp_pointer receiver(ikcp_create(1, &ack_context), &ikcp_release);
    if (!sender || !receiver) throw std::runtime_error("KCP allocation failed");
    for (auto* peer : {sender.get(), receiver.get()}) {
        ikcp_setoutput(peer, kcp_output);
        ikcp_setmtu(peer, max_packet_size);
        ikcp_wndsize(peer, max_inflight, 32);
        // Both modes use a 10ms update interval. Fast enables fast resend and
        // disables congestion control; normal retains normal retransmission/CC.
        ikcp_nodelay(peer, fast ? 1 : 0, update_interval_ms, fast ? 2 : 0, fast ? 1 : 0);
        ikcp_update(peer, 0);
    }
    const int deadline = messages * application_interval_ms + 30000;
    for (; link.now_ms <= deadline && output.valid; ++link.now_ms) {
        const int due = (std::min)(messages, link.now_ms / application_interval_ms + 1);
        while (output.accepted < due && ikcp_waitsnd(sender.get()) < max_inflight) {
            const auto packet = make_message<payload_size>(output.accepted);
            if (ikcp_send(sender.get(), reinterpret_cast<const char*>(&packet), sizeof(packet)) < 0)
                throw std::runtime_error("KCP send failed");
            admitted[output.accepted++] = link.now_ms;
        }
        output.peak_backlog = (std::max)(output.peak_backlog, due - output.accepted);
        if (link.now_ms % update_interval_ms == 0) {
            ikcp_update(sender.get(), link.now_ms);
            ikcp_update(receiver.get(), link.now_ms);
        }
        link.pump(
            [&](std::vector<char>& bytes) {
                if (ikcp_input(receiver.get(), bytes.data(), bytes.size()) < 0) output.valid = false;
            },
            [&](std::vector<char>& bytes) {
                if (ikcp_input(sender.get(), bytes.data(), bytes.size()) < 0) output.valid = false;
            });
        std::array<char, payload_size> bytes{};
        for (int size = ikcp_recv(receiver.get(), bytes.data(), bytes.size()); size >= 0;
             size = ikcp_recv(receiver.get(), bytes.data(), bytes.size())) {
            record<payload_size>(output, bytes.data(), size, link.now_ms, admitted);
        }
        if (output.delivered == messages && ikcp_waitsnd(sender.get()) == 0) break;
    }
    output.pending_at_finish = ikcp_waitsnd(sender.get());
    output.deadline_exceeded = link.now_ms > deadline;
    finish(output, link);
    return output;
}

int percentile(std::vector<int> values, const int percent) {
    if (values.empty()) return -1;
    std::sort(values.begin(), values.end());
    const auto rank = (values.size() * percent + 99) / 100;
    return values[(std::max)(std::size_t{1}, rank) - 1];
}

bool print(const result& output) {
    const bool complete = output.valid && output.accepted == output.offered
        && output.delivered == output.offered && output.pending_at_finish == 0
        && !output.deadline_exceeded && !output.connection_timed_out;
    std::cout << output.protocol << ',' << output.payload_bytes << ',' << output.rtt_ms << ','
              << output.loss_percent << ',' << output.seed << ',' << output.offered << ','
              << output.accepted << ',' << output.delivered << ',' << output.valid << ','
              << complete << ',' << percentile(output.end_to_end_latency, 50) << ','
              << percentile(output.end_to_end_latency, 95) << ','
              << percentile(output.end_to_end_latency, 99) << ','
              << percentile(output.transport_latency, 95) << ',' << output.peak_backlog << ','
              << output.datagrams << ',' << output.dropped << ',' << output.wire_bytes << ','
              << output.finished_ms << ',' << output.final_rto_ms << ','
              << output.connection_timed_out << ',' << output.pending_at_finish << ',' << output.deadline_exceeded << '\n';
    return complete;
}

template <std::size_t payload_size>
bool compare(const int count, const int rtt, const int loss, const std::uint32_t seed) {
    const bool ycnet_ok = print(run_ycnet<payload_size>(count, rtt, loss, seed));
    const bool normal_ok = print(run_kcp<payload_size>(count, rtt, loss, seed, false));
    const bool fast_ok = print(run_kcp<payload_size>(count, rtt, loss, seed, true));
    return ycnet_ok && normal_ok && fast_ok;
}
} // namespace ycnet::benchmark

int main(const int argc, char** argv) {
    using namespace ycnet::benchmark;
    if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--check")) {
        std::cerr << "usage: protocol_comparison [--check]\n";
        return 2;
    }
    const bool check = argc == 2;
    std::cerr << "KCP commit=" << KCP_BASELINE_COMMIT
              << "; virtual clock; 100 messages/s; in-flight limit=16; update=10ms; "
              << "MTU=1024; payload=64/256/512; pre-established sessions; YCNet no-progress deadline=10000ms, max RTO=1000ms; UDP payload bytes incl ACK/drop; no RIO/NIC/CPU measurement\n";
    std::cout << "protocol,payload_bytes,rtt_ms,loss_percent,seed,offered,accepted,delivered,valid,complete,"
                 "p50_end_to_end_ms,p95_end_to_end_ms,p99_end_to_end_ms,p95_transport_ms,"
                 "peak_application_backlog,datagrams,dropped_datagrams,udp_payload_bytes,finished_ms,"
                 "ycnet_final_rto_ms,ycnet_connection_timed_out,pending_at_finish,deadline_exceeded\n";
    bool ok = true;
    if (check) {
        ok = compare<64>(80, 0, 0, 42) && ok;
        ok = compare<256>(80, 80, 0, 43) && ok;
        ok = compare<64>(300, 150, 10, 44) && ok;
    } else {
        for (const auto seed : {42u, 43u, 44u})
            for (const int rtt : {0, 30, 80, 150})
                for (const int loss : {0, 1, 5, 10}) {
                    ok = compare<64>(300, rtt, loss, seed) && ok;
                    ok = compare<256>(300, rtt, loss, seed) && ok;
                    ok = compare<512>(300, rtt, loss, seed) && ok;
                }
    }
    return ok ? 0 : 1;
}

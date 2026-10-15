#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "ycnet/ycnet.hpp"

struct probe_packet {
    static constexpr int packet_id = 91;
    static constexpr bool is_reliable = true;
    std::uint32_t value;
};

int main() {
    std::vector<std::vector<char>> data, acks;
    std::vector<std::uint32_t> delivered;
    std::vector<char> first_data, first_ack;
    std::int64_t now = 0;
    ycnet::rudp_handler sender([](const std::string&) {},
        [&](char* p, std::size_t n) { data.emplace_back(p, p + n); }, [&] { return now; });
    ycnet::rudp_handler receiver([](const std::string&) {},
        [&](char* p, std::size_t n) { acks.emplace_back(p, p + n); }, [&] { return now; });
    packet_events[probe_packet::packet_id] = [&](void* body, packet_size_type, std::size_t) {
        delivered.push_back(static_cast<probe_packet*>(body)->value);
    };
    for (std::uint32_t i = 0; i < 64; ++i) {
        probe_packet p{i};
        if (!sender.send(p)) return 2;
        auto wire = std::move(data.back()); data.clear();
        if (i == 0) first_data = wire;
        receiver.on_receive(wire.data(), wire.size());
        auto ack = std::move(acks.back()); acks.clear();
        if (i == 0) first_ack = ack;
        sender.on_receive(ack.data(), ack.size());
        ++now;
    }
    const auto before = delivered.size();
    receiver.on_receive(first_data.data(), first_data.size());
    std::cout << "delivered_before_old_data=" << before
              << " delivered_after_old_data=" << delivered.size()
              << " last_value=" << delivered.back() << '\n';
    probe_packet current{64};
    sender.send(current); // Intentionally withhold this datagram.
    const auto ack_before = sender.metrics().acks_received;
    sender.on_receive(first_ack.data(), first_ack.size());
    std::cout << "acks_before_old_ack=" << ack_before
              << " acks_after_old_ack=" << sender.metrics().acks_received
              << " new_data_held=" << data.size() << '\n';
}

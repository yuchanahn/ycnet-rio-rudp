#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "ycnet/ycnet.hpp"

ycnet_packet(state, std::uint32_t tick;);

int main() {
    std::vector<std::vector<char>> wire;
    int replies = 0;
    int delivered = 0;
    std::uint32_t received_tick = 0;
    ycnet::rudp_handler sender(
        [](const std::string&) {},
        [&](char* data, const std::size_t size) { wire.emplace_back(data, data + size); }, ycnet::rudp::get_timestamp, ycnet::session_binding{101, 202});
    ycnet::rudp_handler receiver(
        [](const std::string&) {},
        [&](char*, const std::size_t) { ++replies; }, ycnet::rudp::get_timestamp, ycnet::session_binding{202, 101});
    packet_state::bind([&](const packet_state packet, const std::size_t) {
        ++delivered;
        received_tick = packet.tick;
    });

    packet_state first{1};
    packet_state second{2};
    sender.send(first);
    sender.send(second);
    if (wire.size() != 2) return 1;

    // Discard the first update: later unreliable state must still be delivered.
    receiver.on_receive(wire.back().data(), wire.back().size());
    const auto metrics = sender.metrics();
    const bool ok = ycnet::rudp::get_seq(wire.back().data()) == -1
        && delivered == 1 && received_tick == 2 && replies == 0
        && metrics.unreliable_sent == 2 && metrics.reliable_sent == 0;
    std::cout << "delivered=" << delivered << " received_tick=" << received_tick
              << " replies=" << replies << " unreliable_sent=" << metrics.unreliable_sent
              << std::endl;
    return ok ? 0 : 1;
}

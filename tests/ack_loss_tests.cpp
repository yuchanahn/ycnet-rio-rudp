#include <iostream>
#include <string>
#include <vector>

#include "ycnet/ycnet.hpp"
#include "test_packets.hpp"

namespace {
int failures = 0;

void check(const bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << std::endl;
    } else {
        std::cout << "[FAIL] " << name << std::endl;
        ++failures;
    }
}

void test_duplicate_reliable_packet_is_reacked_without_redelivery() {
    std::vector<std::vector<char>> wire_packets;
    std::vector<std::vector<char>> receiver_replies;
    int delivered = 0;

    ycnet::rudp_handler sender(
        [](const std::string&) {},
        [&](char* data, const size_t len) {
            wire_packets.emplace_back(data, data + len);
        }, ycnet::rudp::get_timestamp, ycnet::session_binding{101, 202});

    ycnet::rudp_handler receiver(
        [](const std::string&) {},
        [&](char* data, const size_t len) {
            receiver_replies.emplace_back(data, data + len);
        }, ycnet::rudp::get_timestamp, ycnet::session_binding{202, 101});

    reliable_packet_chat::bind([&](const reliable_packet_chat, const size_t) {
        ++delivered;
    });

    reliable_packet_chat packet{};
    strcpy_s(packet.message, "ack-loss-regression");
    sender.send(packet);

    check(wire_packets.size() == 1, "Sender emits one reliable packet");
    if (wire_packets.empty()) {
        return;
    }

    auto& reliable = wire_packets.front();
    receiver.on_receive(reliable.data(), reliable.size());

    check(delivered == 1, "First reliable packet is delivered once");
    check(receiver_replies.size() == 1 && ycnet::rudp::is_ack_packet(receiver_replies.front().data()),
          "First reliable packet is ACKed");

    receiver_replies.clear(); // Simulate the first ACK being lost in transit.
    receiver.on_receive(reliable.data(), reliable.size());

    check(delivered == 1, "Duplicate reliable packet is not redelivered");
    check(receiver_replies.size() == 1 && ycnet::rudp::is_ack_packet(receiver_replies.front().data()),
          "Duplicate reliable packet is ACKed again after ACK loss");
}
}

int main() {
    test_duplicate_reliable_packet_is_reacked_without_redelivery();
    std::cout << "failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}

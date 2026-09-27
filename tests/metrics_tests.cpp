#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "test_packets.hpp"
#include "ycnet.hpp"

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
}

int main() {
    std::vector<std::vector<char>> wire;
    ycnet::rudp_handler sender(
        [](const std::string&) {},
        [&](char* data, const size_t len) { wire.emplace_back(data, data + len); });

    apacket_chat packet{};
    strcpy_s(packet.message, "metrics");
    check(sender.send(packet), "Reliable send succeeds");

    auto metrics = sender.metrics();
    check(metrics.reliable_sent == 1, "Reliable send counter increments");
    check(metrics.retransmissions == 0, "Retransmission counter starts at zero");
    check(metrics.current_rto_ms == 100, "Initial RTO is exposed");

    std::this_thread::sleep_for(std::chrono::milliseconds(105));
    sender.prc_resend();
    metrics = sender.metrics();
    check(metrics.retransmissions == 1, "Retransmission counter increments on timeout resend");
    check(metrics.current_rto_ms == 200, "RTO metric reflects timeout backoff");

    char ack = ycnet::make_ack_pkt(0);
    sender.on_recv(&ack, 1);
    metrics = sender.metrics();
    check(metrics.acks_received == 1, "ACK receive counter increments");

    std::vector<std::vector<char>> replies;
    int delivered = 0;
    ycnet::rudp_handler receiver(
        [](const std::string&) {},
        [&](char* data, const size_t len) { replies.emplace_back(data, data + len); });
    apacket_chat::bind([&](const apacket_chat, const size_t) { ++delivered; });

    if (!wire.empty()) {
        receiver.on_recv(wire.front().data(), wire.front().size());
        receiver.on_recv(wire.front().data(), wire.front().size());
    }

    const auto receiver_metrics = receiver.metrics();
    check(delivered == 1, "Duplicate packet is delivered once");
    check(receiver_metrics.duplicate_reacks == 1, "Duplicate re-ACK counter increments");

    std::cout << "failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "network_simulator.hpp"
#include "test_packets.hpp"
#include "ycnet.hpp"

namespace {
bool run_loss_case(const int loss_percent) {
    constexpr int kMessages = 80;
    int delivered = 0;

    ycnet::test::deterministic_network link(loss_percent, 0xC0FFEEu);

    ycnet::rudp_handler sender(
        [](const std::string&) {},
        [&](char* data, const size_t len) {
            link.send_a_to_b(data, len);
        });

    ycnet::rudp_handler receiver(
        [](const std::string&) {},
        [&](char* data, const size_t len) {
            link.send_b_to_a(data, len);
        });

    apacket_chat::bind([&](const apacket_chat, const size_t) {
        ++delivered;
    });

    for (int message = 0; message < kMessages; ++message) {
        apacket_chat packet{};
        strcpy_s(packet.message, "loss-test");
        if (!sender.send(packet)) {
            return false;
        }

        const int target = message + 1;
        for (int step = 0; step < 250 && delivered < target; ++step) {
            link.pump(
                [&](char* data, const size_t len) { receiver.on_recv(data, len); },
                [&](char* data, const size_t len) { sender.on_recv(data, len); });

            sender.prc_resend();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        if (delivered != target) {
            return false;
        }

        // Drain the ACK before starting the next message so the send window reflects reality.
        link.pump(
            [&](char* data, const size_t len) { receiver.on_recv(data, len); },
            [&](char* data, const size_t len) { sender.on_recv(data, len); });
    }

    const auto stats = link.stats();
    const bool ok = delivered == kMessages
        && stats.dropped > 0
        && stats.delivered > 0
        && stats.attempted == stats.dropped + stats.delivered;

    std::cout << "loss=" << loss_percent
              << "% delivered_messages=" << delivered
              << " attempted_datagrams=" << stats.attempted
              << " dropped_datagrams=" << stats.dropped
              << " delivered_datagrams=" << stats.delivered
              << std::endl;
    return ok;
}
}

int main() {
    const bool loss_1 = run_loss_case(1);
    const bool loss_5 = run_loss_case(5);
    const bool loss_10 = run_loss_case(10);

    std::cout << "loss_1=" << loss_1
              << " loss_5=" << loss_5
              << " loss_10=" << loss_10
              << std::endl;
    return loss_1 && loss_5 && loss_10 ? 0 : 1;
}

#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>

#include "network_simulator.hpp"
#include "ycnet.hpp"

namespace ycnet::benchmark {
    struct benchmark_packet {
        static constexpr int __packet__id = 90;
        static constexpr bool is_apacket = true;
        std::uint32_t message_index{};
    };

    static_assert(
        benchmark_packet::__packet__id <= (std::numeric_limits<packet_id_type>::max)(),
        "benchmark packet id must fit packet_id_type");

    struct loss_benchmark_result {
        int loss_percent{};
        int messages{};
        int delivered_messages{};
        double delivery_ratio{};
        std::size_t attempted_datagrams{};
        std::size_t dropped_datagrams{};
        std::size_t delivered_datagrams{};
        std::uint64_t retransmissions{};
        int final_rto_ms{};
        double elapsed_ms{};
    };

    inline loss_benchmark_result run_loss_benchmark(
        const int messages,
        const int loss_percent,
        const std::uint32_t seed = 0xC0FFEEu) {
        int delivered = 0;
        ycnet::test::deterministic_network link(loss_percent, seed);

        ycnet::rudp_handler sender(
            [](const std::string&) {},
            [&](char* data, const size_t len) { link.send_a_to_b(data, len); });

        ycnet::rudp_handler receiver(
            [](const std::string&) {},
            [&](char* data, const size_t len) { link.send_b_to_a(data, len); });

        packet_events[benchmark_packet::__packet__id] =
            [&](void*, const packet_size_type, const size_t) { ++delivered; };

        const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < messages; ++i) {
            benchmark_packet packet{static_cast<std::uint32_t>(i)};
            if (!sender.send(packet)) {
                break;
            }

            const int target = i + 1;
            for (int step = 0; step < 1000 && delivered < target; ++step) {
                link.pump(
                    [&](char* data, const size_t len) { receiver.on_recv(data, len); },
                    [&](char* data, const size_t len) { sender.on_recv(data, len); });
                sender.prc_resend();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }

            link.pump(
                [&](char* data, const size_t len) { receiver.on_recv(data, len); },
                [&](char* data, const size_t len) { sender.on_recv(data, len); });
        }

        for (int step = 0; step < 100; ++step) {
            link.pump(
                [&](char* data, const size_t len) { receiver.on_recv(data, len); },
                [&](char* data, const size_t len) { sender.on_recv(data, len); });
            sender.prc_resend();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        const auto finished = std::chrono::steady_clock::now();
        const auto network = link.stats();
        const auto rudp = sender.metrics();
        const double elapsed = std::chrono::duration<double, std::milli>(finished - started).count();

        return loss_benchmark_result{
            .loss_percent = loss_percent,
            .messages = messages,
            .delivered_messages = delivered,
            .delivery_ratio = messages > 0 ? static_cast<double>(delivered) / messages : 1.0,
            .attempted_datagrams = network.attempted,
            .dropped_datagrams = network.dropped,
            .delivered_datagrams = network.delivered,
            .retransmissions = rudp.retransmissions,
            .final_rto_ms = rudp.current_rto_ms,
            .elapsed_ms = elapsed,
        };
    }
}

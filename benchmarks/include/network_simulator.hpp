#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace ycnet::test {
    struct network_stats {
        std::size_t attempted{};
        std::size_t dropped{};
        std::size_t delivered{};
    };

    class deterministic_network {
        using datagram = std::vector<char>;

        int loss_percent_{};
        int loss_accumulator_{};
        network_stats stats_{};
        std::deque<datagram> a_to_b_{};
        std::deque<datagram> b_to_a_{};

        bool should_drop() {
            ++stats_.attempted;
            loss_accumulator_ += loss_percent_;
            if (loss_accumulator_ < 100) {
                return false;
            }

            loss_accumulator_ -= 100;
            ++stats_.dropped;
            return true;
        }

        void enqueue(std::deque<datagram>& queue, char* data, const std::size_t len) {
            if (should_drop()) {
                return;
            }
            queue.emplace_back(data, data + len);
        }

        template <typename Receiver>
        void drain(std::deque<datagram>& queue, Receiver&& receiver) {
            std::deque<datagram> pending;
            pending.swap(queue);
            while (!pending.empty()) {
                auto packet = std::move(pending.front());
                pending.pop_front();
                ++stats_.delivered;
                receiver(packet.data(), packet.size());
            }
        }

    public:
        explicit deterministic_network(const int loss_percent, const std::uint32_t seed = 0)
            : loss_percent_(loss_percent < 0 ? 0 : (loss_percent > 100 ? 100 : loss_percent))
            , loss_accumulator_(static_cast<int>(seed % 100)) {
        }

        void send_a_to_b(char* data, const std::size_t len) {
            enqueue(a_to_b_, data, len);
        }

        void send_b_to_a(char* data, const std::size_t len) {
            enqueue(b_to_a_, data, len);
        }

        template <typename ReceiverB, typename ReceiverA>
        void pump(ReceiverB&& receiver_b, ReceiverA&& receiver_a) {
            drain(a_to_b_, std::forward<ReceiverB>(receiver_b));
            drain(b_to_a_, std::forward<ReceiverA>(receiver_a));
        }

        [[nodiscard]] network_stats stats() const {
            return stats_;
        }
    };
}

#include <iostream>

#include "loss_benchmark.hpp"

int main() {
    const auto result = ycnet::benchmark::run_loss_benchmark(40, 5, 0xBADC0DEu);

    const bool ok = result.messages == 40
        && result.delivered_messages == 40
        && result.delivery_ratio == 1.0
        && result.dropped_datagrams > 0
        && result.retransmissions > 0
        && result.final_rto_ms >= 10
        && result.elapsed_ms >= 0.0;

    std::cout << "messages=" << result.messages
              << " delivered=" << result.delivered_messages
              << " dropped=" << result.dropped_datagrams
              << " retransmissions=" << result.retransmissions
              << " final_rto_ms=" << result.final_rto_ms
              << " elapsed_ms=" << result.elapsed_ms
              << std::endl;
    return ok ? 0 : 1;
}

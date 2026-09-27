#include <cstdlib>
#include <iostream>

#include "loss_benchmark.hpp"

int main(const int argc, char** argv) {
    int messages = 200;
    if (argc > 1) {
        messages = std::atoi(argv[1]);
        if (messages <= 0) messages = 200;
    }

    std::cout << "loss_percent,messages,delivered,delivery_ratio,attempted_datagrams,dropped_datagrams,"
                 "delivered_datagrams,retransmissions,final_rto_ms,elapsed_ms"
              << std::endl;

    for (const int loss : {0, 1, 5, 10}) {
        const auto result = ycnet::benchmark::run_loss_benchmark(messages, loss);
        std::cout << result.loss_percent << ','
                  << result.messages << ','
                  << result.delivered_messages << ','
                  << result.delivery_ratio << ','
                  << result.attempted_datagrams << ','
                  << result.dropped_datagrams << ','
                  << result.delivered_datagrams << ','
                  << result.retransmissions << ','
                  << result.final_rto_ms << ','
                  << result.elapsed_ms
                  << std::endl;
    }
}

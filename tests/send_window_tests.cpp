#include <iostream>
#include <string>

#include "ycnet/ycnet.hpp"
#include "test_packets.hpp"

int main() {
    int datagrams_sent = 0;
    int window_full_logs = 0;

    ycnet::rudp_handler handler(
        [&](const std::string& msg) {
            if (msg.find("window full") != std::string::npos) {
                ++window_full_logs;
            }
        },
        [&](char*, const size_t) {
            ++datagrams_sent;
        }, ycnet::rudp::get_timestamp, ycnet::session_binding{101, 202});

    reliable_packet_chat packet{};
    strcpy_s(packet.message, "window-capacity");

    bool first_window_accepted = true;
    for (int i = 0; i < reliable_window_size; ++i) {
        first_window_accepted = handler.send(packet) && first_window_accepted;
    }

    const bool overflow_accepted = handler.send(packet);

    const bool ok = first_window_accepted
        && !overflow_accepted
        && datagrams_sent == reliable_window_size
        && window_full_logs == 1;

    std::cout << "first_window_accepted=" << first_window_accepted
              << " overflow_accepted=" << overflow_accepted
              << " datagrams_sent=" << datagrams_sent
              << " window_full_logs=" << window_full_logs
              << std::endl;
    return ok ? 0 : 1;
}

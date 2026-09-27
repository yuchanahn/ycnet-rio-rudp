#include <WinSock2.h>
#include <WS2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

#include "test_packets.hpp"
#include "ycnet.hpp"

int main() {
    constexpr unsigned short kPort = 17781;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(kPort);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    const SOCKET socket_a = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const SOCKET socket_b = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_a == INVALID_SOCKET || socket_b == INVALID_SOCKET) return 1;

    u_long nonblocking = 1;
    ioctlsocket(socket_a, FIONBIO, &nonblocking);
    ioctlsocket(socket_b, FIONBIO, &nonblocking);

    std::atomic_int connected = 0;
    std::atomic_int timed_out = 0;
    ycnet::core::endpoint_t first_endpoint{};
    ycnet::core::endpoint_t second_endpoint{};

    ycnet::rudp_server<1, 1, kPort> server([](const std::string&) {});
    server.set_time_out_rtt(150);
    server.set_connect_func([&](const ycnet::core::endpoint_t endpoint, const size_t) {
        const int count = ++connected;
        if (count == 1) first_endpoint = endpoint;
        if (count == 2) second_endpoint = endpoint;
    });
    server.set_time_out_func([&](const ycnet::core::endpoint_t, const size_t) { ++timed_out; });
    apacket_chat::bind([](const apacket_chat, const size_t) {});
    server.io_service_run();

    auto make_client = [&](const SOCKET handle) {
        return std::make_unique<ycnet::rudp_handler>(
            [](const std::string&) {},
            [&, handle](char* data, const size_t len) {
                sendto(handle, data, static_cast<int>(len), 0,
                       reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
            });
    };

    auto client_a = make_client(socket_a);
    apacket_chat hello_a{};
    strcpy_s(hello_a.message, "timeout-a");
    if (!client_a->send(hello_a)) return 1;

    for (int step = 0; step < 100 && connected.load() < 1; ++step) {
        server.intr_pkt();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    apacket_chat server_packet{};
    strcpy_s(server_packet.message, "force-timeout");
    if (!server.send(server_packet, 0)) return 1;

    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    server.prc_resend(0);

    const bool old_mapping_removed_after_timeout = !server.user_id(first_endpoint).has_value();

    auto client_b = make_client(socket_b);
    apacket_chat hello_b{};
    strcpy_s(hello_b.message, "timeout-b");
    if (!client_b->send(hello_b)) return 1;

    for (int step = 0; step < 100 && connected.load() < 2; ++step) {
        server.intr_pkt();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto old_id = server.user_id(first_endpoint);
    const auto new_id = server.user_id(second_endpoint);

    const auto kill_result = server.kill();
    closesocket(socket_a);
    closesocket(socket_b);
    WSACleanup();

    const bool ok = kill_result.has_value()
        && connected.load() == 2
        && timed_out.load() == 1
        && old_mapping_removed_after_timeout
        && !old_id.has_value()
        && new_id.has_value()
        && *new_id == 0;

    std::cout << "connected=" << connected.load()
              << " timed_out=" << timed_out.load()
              << " old_mapping_removed_after_timeout=" << old_mapping_removed_after_timeout
              << " old_mapping_present_after_reuse=" << old_id.has_value()
              << " new_id=" << (new_id.has_value() ? std::to_string(*new_id) : "none")
              << std::endl;
    return ok ? 0 : 1;
}

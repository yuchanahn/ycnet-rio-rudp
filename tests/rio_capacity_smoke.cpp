#include <WinSock2.h>
#include <WS2tcpip.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "test_packets.hpp"
#include "ycnet.hpp"

int main() {
    constexpr unsigned short kPort = 17780;
    constexpr int kMaxClients = 2;
    constexpr int kAttemptedClients = 3;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(kPort);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    std::array<SOCKET, kAttemptedClients> sockets{};
    sockets.fill(INVALID_SOCKET);
    for (auto& socket_handle : sockets) {
        socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_handle == INVALID_SOCKET) return 1;
        u_long nonblocking = 1;
        ioctlsocket(socket_handle, FIONBIO, &nonblocking);
    }

    std::atomic_int connected = 0;
    std::atomic_int delivered = 0;
    std::atomic_int capacity_logs = 0;
    std::array<std::atomic_bool, kAttemptedClients> acked{};

    ycnet::rudp_server<kMaxClients, 1, kPort> server([&](const std::string& msg) {
        if (msg.find("capacity") != std::string::npos) ++capacity_logs;
    });
    server.set_connect_func([&](const ycnet::core::endpoint_t, const size_t) { ++connected; });
    server.set_time_out_func([](const ycnet::core::endpoint_t, const size_t) {});

    apacket_chat::bind([&](const apacket_chat, const size_t) { ++delivered; });
    server.io_service_run();

    std::vector<std::unique_ptr<ycnet::rudp_handler>> clients;
    for (int i = 0; i < kAttemptedClients; ++i) {
        clients.push_back(std::make_unique<ycnet::rudp_handler>(
            [](const std::string&) {},
            [&, i](char* data, const size_t len) {
                sendto(sockets[i], data, static_cast<int>(len), 0,
                       reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
            }));

        apacket_chat packet{};
        const auto text = "capacity-" + std::to_string(i);
        strcpy_s(packet.message, text.c_str());
        clients.back()->send(packet);
    }

    for (int step = 0; step < 250; ++step) {
        server.intr_pkt();
        for (int i = 0; i < kAttemptedClients; ++i) {
            char buffer[1024]{};
            sockaddr_in from{};
            int from_len = sizeof(from);
            const int len = recvfrom(sockets[i], buffer, static_cast<int>(sizeof(buffer)), 0,
                                     reinterpret_cast<sockaddr*>(&from), &from_len);
            if (len > 0) {
                acked[i] = yc_rudp::is_ack_packet(buffer);
                clients[i]->on_recv(buffer, static_cast<size_t>(len));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto kill_result = server.kill();
    for (const auto socket_handle : sockets) closesocket(socket_handle);
    WSACleanup();

    int ack_count = 0;
    for (const auto& ack : acked) ack_count += ack.load() ? 1 : 0;

    const bool ok = kill_result.has_value()
        && connected.load() == kMaxClients
        && delivered.load() == kMaxClients
        && ack_count == kMaxClients
        && capacity_logs.load() >= 1;

    std::cout << "connected=" << connected.load()
              << " delivered=" << delivered.load()
              << " acked=" << ack_count
              << " capacity_logs=" << capacity_logs.load()
              << std::endl;
    return ok ? 0 : 1;
}

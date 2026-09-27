#include <WinSock2.h>
#include <WS2tcpip.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "test_packets.hpp"
#include "ycnet.hpp"

int main() {
    constexpr unsigned short kPort = 17779;
    constexpr int kClients = 8;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(kPort);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    std::array<SOCKET, kClients> sockets{};
    sockets.fill(INVALID_SOCKET);

    for (auto& socket_handle : sockets) {
        socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_handle == INVALID_SOCKET) {
            WSACleanup();
            return 1;
        }
        u_long nonblocking = 1;
        ioctlsocket(socket_handle, FIONBIO, &nonblocking);
    }

    std::atomic_int connected = 0;
    std::atomic_int delivered = 0;
    std::array<std::atomic_bool, kClients> acked{};
    std::set<size_t> client_ids;
    std::string server_error;

    ycnet::rudp_server<16, 2, kPort> server([&](const std::string& msg) {
        if (msg.find("error") != std::string::npos || msg.find("failed") != std::string::npos) {
            server_error = msg;
        }
    });

    server.set_connect_func([&](const ycnet::core::endpoint_t, const size_t client_id) {
        client_ids.insert(client_id);
        ++connected;
    });
    server.set_time_out_func([](const ycnet::core::endpoint_t, const size_t) {});

    apacket_chat::bind([&](const apacket_chat packet, const size_t) {
        if (std::string(packet.message).starts_with("client-")) {
            ++delivered;
        }
    });

    server.io_service_run();
    if (!server_error.empty()) {
        for (const auto socket_handle : sockets) closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    std::vector<std::unique_ptr<ycnet::rudp_handler>> clients;
    clients.reserve(kClients);
    for (int i = 0; i < kClients; ++i) {
        clients.push_back(std::make_unique<ycnet::rudp_handler>(
            [](const std::string&) {},
            [&, i](char* data, const size_t len) {
                sendto(
                    sockets[i],
                    data,
                    static_cast<int>(len),
                    0,
                    reinterpret_cast<const sockaddr*>(&server_addr),
                    sizeof(server_addr));
            }));

        apacket_chat packet{};
        const auto text = "client-" + std::to_string(i);
        strcpy_s(packet.message, text.c_str());
        if (!clients.back()->send(packet)) {
            return 1;
        }
    }

    for (int step = 0; step < 500 && delivered.load() < kClients; ++step) {
        server.intr_pkt();

        for (int i = 0; i < kClients; ++i) {
            for (;;) {
                char buffer[1024]{};
                sockaddr_in from{};
                int from_len = sizeof(from);
                const int len = recvfrom(
                    sockets[i],
                    buffer,
                    static_cast<int>(sizeof(buffer)),
                    0,
                    reinterpret_cast<sockaddr*>(&from),
                    &from_len);

                if (len == SOCKET_ERROR) {
                    if (WSAGetLastError() != WSAEWOULDBLOCK) {
                        server_error = "recvfrom failed";
                    }
                    break;
                }

                if (len > 0) {
                    if (yc_rudp::is_ack_packet(buffer)) acked[i] = true;
                    clients[i]->on_recv(buffer, static_cast<size_t>(len));
                }
            }
            clients[i]->prc_resend();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    for (int step = 0; step < 100; ++step) {
        server.intr_pkt();
        bool all_acked = true;
        for (int i = 0; i < kClients; ++i) {
            char buffer[1024]{};
            sockaddr_in from{};
            int from_len = sizeof(from);
            const int len = recvfrom(
                sockets[i], buffer, static_cast<int>(sizeof(buffer)), 0,
                reinterpret_cast<sockaddr*>(&from), &from_len);
            if (len > 0) {
                if (yc_rudp::is_ack_packet(buffer)) acked[i] = true;
                clients[i]->on_recv(buffer, static_cast<size_t>(len));
            }
            all_acked = all_acked && acked[i].load();
        }
        if (all_acked) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto kill_result = server.kill();
    if (!kill_result) {
        server_error = kill_result.err;
    }

    for (const auto socket_handle : sockets) closesocket(socket_handle);
    WSACleanup();

    int ack_count = 0;
    for (const auto& ack : acked) ack_count += ack.load() ? 1 : 0;

    const bool ok = server_error.empty()
        && connected.load() == kClients
        && delivered.load() == kClients
        && ack_count == kClients
        && client_ids.size() == kClients;

    std::cout << "connected=" << connected.load()
              << " unique_ids=" << client_ids.size()
              << " delivered=" << delivered.load()
              << " acked=" << ack_count
              << std::endl;
    return ok ? 0 : 1;
}

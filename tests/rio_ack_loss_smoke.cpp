#include <WinSock2.h>
#include <WS2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "ycnet/ycnet.hpp"
#include "test_packets.hpp"

int main() {
    constexpr unsigned short kPort = 17778;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 1;
    }

    SOCKET client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client_socket == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    u_long nonblocking = 1;
    ioctlsocket(client_socket, FIONBIO, &nonblocking);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(kPort);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    std::atomic_int delivered = 0;
    std::atomic_int datagrams_sent = 0;
    std::atomic_int acks_seen = 0;
    std::string server_error;

    ycnet::rudp_server<16, 1, kPort> server([&](const std::string& msg) {
        if (msg.find("error") != std::string::npos || msg.find("failed") != std::string::npos) {
            server_error = msg;
        }
    });
    server.set_connect_callback([](const ycnet::rio::endpoint, const size_t) {});
    server.set_timeout_callback([](const ycnet::rio::endpoint, const size_t) {});

    reliable_packet_chat::bind([&](const reliable_packet_chat, const size_t) {
        ++delivered;
    });

    server.start_io();
    if (!server_error.empty()) {
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    ycnet::rudp_handler client(
        [](const std::string&) {},
        [&](char* data, const size_t len) {
            ++datagrams_sent;
            sendto(
                client_socket,
                data,
                static_cast<int>(len),
                0,
                reinterpret_cast<const sockaddr*>(&server_addr),
                sizeof(server_addr));
        });

    reliable_packet_chat packet{};
    strcpy_s(packet.message, "drop-first-ack");
    client.send(packet);

    const auto drain_ack = [&]() {
        char buffer[1024]{};
        sockaddr_in from{};
        int from_len = sizeof(from);
        const int len = recvfrom(
            client_socket,
            buffer,
            static_cast<int>(sizeof(buffer)),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &from_len);
        if (len > 0) {
            if (ycnet::rudp::is_ack_packet(buffer)) {
                const auto count = ++acks_seen;
                if (count > 1) client.on_receive(buffer, static_cast<size_t>(len));
                return true;
            }
            client.on_receive(buffer, static_cast<size_t>(len));
        }
        return false;
    };

    for (int step = 0; step < 100 && acks_seen.load() == 0; ++step) {
        server.process_received_packets();
        drain_ack(); // Intentionally discard the first ACK instead of passing it to client.on_receive().
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    client.process_retransmissions();

    for (int step = 0; step < 100 && acks_seen.load() < 2; ++step) {
        server.process_received_packets();
        drain_ack();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto kill_result = server.stop();
    if (!kill_result) {
        std::cerr << "server stop failed: " << kill_result.err << std::endl;
    }
    closesocket(client_socket);
    WSACleanup();

    const bool ok = delivered.load() == 1 && datagrams_sent.load() >= 2 && acks_seen.load() >= 2;
    std::cout << "delivered=" << delivered.load()
              << " datagrams_sent=" << datagrams_sent.load()
              << " acks_seen=" << acks_seen.load()
              << std::endl;
    return ok ? 0 : 1;
}

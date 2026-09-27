#include <WinSock2.h>
#include <WS2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "ycnet.hpp"
#include "test_packets.hpp"

int main() {
    constexpr unsigned short kPort = 17777;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    SOCKET client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "client socket failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    u_long nonblocking = 1;
    ioctlsocket(client_socket, FIONBIO, &nonblocking);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(kPort);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    std::atomic_bool connected = false;
    std::atomic_bool received = false;
    std::string received_text;
    std::string server_error;

    ycnet::rudp_server<16, 1, kPort> server([&](const std::string& msg) {
        std::cout << "[server] " << msg << std::endl;
        if (msg.find("error") != std::string::npos || msg.find("failed") != std::string::npos) {
            server_error = msg;
        }
    });

    server.set_connect_func([&](const ycnet::core::endpoint_t ep, size_t client_id) {
        connected = true;
        std::cout << "[connect] " << ep.to_string() << " id=" << client_id << std::endl;
    });
    server.set_time_out_func([](const ycnet::core::endpoint_t ep, size_t client_id) {
        std::cout << "[timeout] " << ep.to_string() << " id=" << client_id << std::endl;
    });

    apacket_chat::bind([&](apacket_chat packet, size_t client_id) {
        received_text = packet.message;
        received = true;
        std::cout << "[packet] id=" << client_id << " text=" << received_text << std::endl;
    });

    server.io_service_run();
    if (!server_error.empty()) {
        std::cerr << "server init failed: " << server_error << std::endl;
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    ycnet::rudp_handler client(
        [](const std::string& msg) { std::cout << "[client] " << msg << std::endl; },
        [&](char* data, size_t len) {
            const int sent = sendto(
                client_socket,
                data,
                static_cast<int>(len),
                0,
                reinterpret_cast<const sockaddr*>(&server_addr),
                sizeof(server_addr));
            if (sent == SOCKET_ERROR) {
                std::cerr << "sendto failed: " << WSAGetLastError() << std::endl;
            }
        });

    apacket_chat packet{};
    strcpy_s(packet.message, "ycnet-rio-smoke");
    client.send(packet);

    bool got_ack = false;
    for (int step = 0; step < 400 && !received.load(); ++step) {
        server.intr_pkt();

        for (;;) {
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

            if (len == SOCKET_ERROR) {
                const int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK) {
                    std::cerr << "recvfrom failed: " << err << std::endl;
                }
                break;
            }

            if (len > 0) {
                got_ack = got_ack || yc_rudp::is_ack_packet(buffer);
                client.on_recv(buffer, static_cast<size_t>(len));
            }
        }

        client.prc_resend();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    for (int step = 0; step < 40 && !got_ack; ++step) {
        server.intr_pkt();
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
            got_ack = got_ack || yc_rudp::is_ack_packet(buffer);
            client.on_recv(buffer, static_cast<size_t>(len));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto kill_result = server.kill();
    if (!kill_result) {
        std::cerr << "server kill failed: " << kill_result.err << std::endl;
    }

    closesocket(client_socket);
    WSACleanup();

    const bool ok = connected.load() && received.load() && got_ack && received_text == "ycnet-rio-smoke";
    std::cout << "connected=" << connected.load()
              << " received=" << received.load()
              << " ack=" << got_ack
              << " payload_ok=" << (received_text == "ycnet-rio-smoke")
              << std::endl;
    return ok ? 0 : 1;
}

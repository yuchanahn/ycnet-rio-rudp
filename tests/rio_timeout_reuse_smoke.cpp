#include <WinSock2.h>
#include <WS2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "test_packets.hpp"
#include "ycnet/ycnet.hpp"

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
    int delivered = 0;
    ycnet::rio::endpoint first_endpoint{};
    ycnet::rio::endpoint second_endpoint{};

    ycnet::rudp_server<1, 1, kPort> server([](const std::string&) {});
    server.set_progress_timeout(150);
    server.set_connect_callback([&](const ycnet::rio::endpoint endpoint, const size_t) {
        const int count = ++connected;
        if (count == 1) first_endpoint = endpoint;
        if (count == 2) second_endpoint = endpoint;
    });
    server.set_timeout_callback([&](const ycnet::rio::endpoint, const size_t) { ++timed_out; });
    reliable_packet_chat::bind([&](const reliable_packet_chat, const size_t) { ++delivered; });
    server.start_io();

    std::vector<char> old_data;
    std::vector<char> old_server_ack;
    auto drain = [&](SOCKET handle, ycnet::rudp_handler& client) {
        char data[1024];
        int len;
        while ((len = recvfrom(handle, data, sizeof(data), 0, nullptr, nullptr)) > 0) {
            if (handle == socket_a && old_server_ack.empty() && ycnet::rudp::is_ack_packet(data))
                old_server_ack.assign(data, data + len);
            client.on_receive(data, static_cast<size_t>(len));
        }
    };
    auto make_client = [&](const SOCKET handle) {
        return std::make_unique<ycnet::rudp_handler>(
            [](const std::string&) {},
            [&, handle](char* data, const size_t len) {
                ycnet::packet::udp::ack_header header;
                header.load(data);
                if (handle == socket_a && header.use_ack && !header.is_ack_packet)
                    old_data.assign(data, data + len);
                sendto(handle, data, static_cast<int>(len), 0,
                       reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
            });
    };

    auto client_a = make_client(socket_a);
    reliable_packet_chat hello_a{};
    strcpy_s(hello_a.message, "timeout-a");
    if (!client_a->send(hello_a)) return 1;

    for (int step = 0; step < 100 && connected.load() < 1; ++step) {
        server.process_received_packets();
        drain(socket_a, *client_a);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    reliable_packet_chat server_packet{};
    strcpy_s(server_packet.message, "force-timeout");
    if (!server.send(server_packet, 0)) return 1;

    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    server.process_retransmissions(0);

    const bool old_mapping_removed_after_timeout = !server.user_id(first_endpoint).has_value();

    // Delayed old DATA cannot allocate a new connection after timeout.
    sendto(socket_a, old_data.data(), static_cast<int>(old_data.size()), 0,
           reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
    for (int step = 0; step < 10; ++step) {
        server.process_received_packets();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const bool replay_did_not_reconnect = connected.load() == 1 && !server.user_id(first_endpoint).has_value();

    auto client_b = make_client(socket_b);
    reliable_packet_chat hello_b{};
    strcpy_s(hello_b.message, "timeout-b");
    if (!client_b->send(hello_b)) return 1;

    for (int step = 0; step < 100 && connected.load() < 2; ++step) {
        server.process_received_packets();
        drain(socket_b, *client_b);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto old_id = server.user_id(first_endpoint);
    const auto new_id = server.user_id(second_endpoint);

    // Reconnect using the exact same socket/address and packet number zero.
    const auto saved_old_data = old_data;
    server.disconnect(0);
    auto replacement_a = make_client(socket_a);
    if (!replacement_a->send(hello_a)) return 1;
    bool old_ack_rejected = false;
    for (int step = 0; step < 100 && connected.load() < 3; ++step) {
        server.process_received_packets();
        drain(socket_a, *replacement_a);
        if (!old_ack_rejected && replacement_a->state() == ycnet::connection_state::connected && !old_server_ack.empty()) {
            const auto count = replacement_a->metrics().acks_received;
            replacement_a->on_receive(old_server_ack.data(), old_server_ack.size());
            old_ack_rejected = replacement_a->metrics().acks_received == count
                && replacement_a->metrics().pending_reliable == 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const int before_replay = delivered;
    sendto(socket_a, saved_old_data.data(), static_cast<int>(saved_old_data.size()), 0,
           reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
    for (int step = 0; step < 10; ++step) {
        server.process_received_packets();
        drain(socket_a, *replacement_a);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const bool same_endpoint_replay_rejected = old_ack_rejected && before_replay == 3
        && delivered == 3 && server.user_id(first_endpoint).has_value();

    const auto kill_result = server.stop();
    closesocket(socket_a);
    closesocket(socket_b);
    WSACleanup();

    const bool ok = kill_result.has_value()
        && connected.load() == 3
        && same_endpoint_replay_rejected
        && timed_out.load() == 1
        && replay_did_not_reconnect
        && old_mapping_removed_after_timeout
        && !old_id.has_value()
        && new_id.has_value()
        && *new_id == 0;

    std::cout << "connected=" << connected.load()
              << " timed_out=" << timed_out.load()
              << " same_endpoint_replay_rejected=" << same_endpoint_replay_rejected
              << " old_mapping_removed_after_timeout=" << old_mapping_removed_after_timeout
              << " old_mapping_present_after_reuse=" << old_id.has_value()
              << " new_id=" << (new_id.has_value() ? std::to_string(*new_id) : "none")
              << std::endl;
    return ok ? 0 : 1;
}

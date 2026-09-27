#include <WinSock2.h>
#include <WS2tcpip.h>

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "arena_server.hpp"

namespace {
bool nearly_equal(const float lhs, const float rhs) {
    return std::abs(lhs - rhs) < 0.05f;
}
}

int main() {
    constexpr unsigned short kPort = 17782;
    constexpr int kClients = 2;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(kPort);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    std::array<SOCKET, kClients> sockets{};
    sockets.fill(INVALID_SOCKET);
    for (auto& handle : sockets) {
        handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (handle == INVALID_SOCKET) return 1;
        u_long nonblocking = 1;
        ioctlsocket(handle, FIONBIO, &nonblocking);
    }

    std::string server_error;
    arena::network_server<4, 1, kPort> server([&](const std::string& message) {
        if (message.find("failed") != std::string::npos || message.find("error") != std::string::npos) {
            server_error = message;
        }
    });
    server.start();

    std::array<std::unique_ptr<ycnet::rudp_handler>, kClients> clients;
    for (int i = 0; i < kClients; ++i) {
        clients[i] = std::make_unique<ycnet::rudp_handler>(
            [](const std::string&) {},
            [&, i](char* data, const size_t len) {
                sendto(sockets[i], data, static_cast<int>(len), 0,
                       reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
            });
    }

    int dispatch_client = -1;
    std::array<int, kClients> snapshots_seen{};
    std::array<int, kClients> damage_seen{};
    std::array<int, kClients> last_damage_hp{-1, -1};
    std::array<std::unordered_map<std::uint32_t, arena::snapshot_state>, kClients> snapshots;

    packet_arena_snapshot::bind([&](const packet_arena_snapshot packet, const size_t) {
        if (dispatch_client < 0) return;
        ++snapshots_seen[dispatch_client];
        snapshots[dispatch_client][packet.player_id] = {
            packet.player_id, packet.x, packet.y, packet.hp, packet.alive != 0};
    });
    apacket_arena_damage::bind([&](const apacket_arena_damage packet, const size_t) {
        if (dispatch_client < 0) return;
        ++damage_seen[dispatch_client];
        last_damage_hp[dispatch_client] = packet.hp;
    });

    auto drain_clients = [&] {
        for (int i = 0; i < kClients; ++i) {
            for (;;) {
                char buffer[1024]{};
                sockaddr_in from{};
                int from_len = sizeof(from);
                const int len = recvfrom(
                    sockets[i], buffer, static_cast<int>(sizeof(buffer)), 0,
                    reinterpret_cast<sockaddr*>(&from), &from_len);
                if (len == SOCKET_ERROR) {
                    if (WSAGetLastError() != WSAEWOULDBLOCK) server_error = "recvfrom failed";
                    break;
                }
                if (len > 0) {
                    dispatch_client = i;
                    clients[i]->on_recv(buffer, static_cast<size_t>(len));
                    dispatch_client = -1;
                }
            }
            clients[i]->prc_resend();
        }
    };

    for (int i = 0; i < kClients; ++i) {
        apacket_arena_join join{static_cast<std::uint32_t>(100 + i)};
        if (!clients[i]->send(join)) return 1;
    }

    for (int step = 0; step < 250 && server.connected_count() < kClients; ++step) {
        server.poll();
        drain_clients();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto client0_id = server.client_id_for_nonce(100);
    const auto client1_id = server.client_id_for_nonce(101);
    if (!client0_id || !client1_id || *client0_id == *client1_id) return 1;

    const float input_x = *client0_id % 2 == 0 ? 1.0f : -1.0f;
    packet_arena_input input{input_x, 0.0f};
    clients[0]->send(input);
    for (int step = 0; step < 100 && server.input_events() < 1; ++step) {
        server.poll();
        drain_clients();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    server.tick(0.25f);

    for (int step = 0; step < 100 && (snapshots_seen[0] < 2 || snapshots_seen[1] < 2); ++step) {
        server.poll();
        drain_clients();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    apacket_arena_attack attack{static_cast<std::uint32_t>(*client1_id)};
    if (!clients[0]->send(attack)) return 1;
    for (int step = 0; step < 200 && (damage_seen[0] < 1 || damage_seen[1] < 1); ++step) {
        server.poll();
        server.process_resends();
        drain_clients();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    for (int step = 0; step < 30; ++step) {
        server.poll();
        server.process_resends();
        drain_clients();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto moving_player = server.player(*client0_id);
    const auto target_player = server.player(*client1_id);
    const bool stopped = server.stop();

    for (const auto handle : sockets) closesocket(handle);
    WSACleanup();

    const auto moving_id = static_cast<std::uint32_t>(*client0_id);
    const bool client0_has_moving_player = snapshots[0].contains(moving_id);
    const bool client1_has_moving_player = snapshots[1].contains(moving_id);
    const bool snapshot_position_ok = client0_has_moving_player
        && nearly_equal(snapshots[0][moving_id].x, 0.0f)
        && client1_has_moving_player
        && nearly_equal(snapshots[1][moving_id].x, 0.0f);

    const bool ok = server_error.empty()
        && stopped
        && server.connected_count() == kClients
        && server.input_events() >= 1
        && server.accepted_attacks() == 1
        && moving_player.has_value() && nearly_equal(moving_player->position.x, 0.0f)
        && target_player.has_value() && target_player->hp == 75
        && snapshots_seen[0] >= 2 && snapshots_seen[1] >= 2
        && snapshot_position_ok
        && damage_seen[0] == 1 && damage_seen[1] == 1
        && last_damage_hp[0] == 75 && last_damage_hp[1] == 75;

    std::cout << "connected=" << server.connected_count()
              << " input_events=" << server.input_events()
              << " attacks=" << server.accepted_attacks()
              << " mover_id=" << *client0_id
              << " target_id=" << *client1_id
              << " mover_x=" << (moving_player ? moving_player->position.x : -999.0f)
              << " target_hp=" << (target_player ? target_player->hp : -1)
              << " snapshots=" << snapshots_seen[0] << '/' << snapshots_seen[1]
              << " damage=" << damage_seen[0] << '/' << damage_seen[1]
              << std::endl;
    return ok ? 0 : 1;
}

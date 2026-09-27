#include <WinSock2.h>
#include <WS2tcpip.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "arena_server.hpp"

namespace {
constexpr unsigned short kPort = 17783;
constexpr int kClientCount = 2;
constexpr float kTickSeconds = 0.05f;

struct client_view {
    std::unordered_map<std::uint32_t, arena::snapshot_state> players;
    int damage_events{};
};

void print_state(
    const int tick,
    const arena::network_server<4, 1, kPort>& server,
    const std::size_t first_id,
    const std::size_t second_id) {
    const auto first = server.player(first_id);
    const auto second = server.player(second_id);
    if (!first || !second) return;

    std::cout << "tick=" << tick
              << " | P" << first_id << " x=" << first->position.x << " hp=" << first->hp
              << " | P" << second_id << " x=" << second->position.x << " hp=" << second->hp
              << std::endl;
}
}

int main() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(kPort);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    std::array<SOCKET, kClientCount> sockets{};
    sockets.fill(INVALID_SOCKET);
    for (auto& handle : sockets) {
        handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (handle == INVALID_SOCKET) {
            std::cerr << "socket creation failed" << std::endl;
            WSACleanup();
            return 1;
        }
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

    std::array<std::unique_ptr<ycnet::rudp_handler>, kClientCount> clients;
    for (int i = 0; i < kClientCount; ++i) {
        clients[i] = std::make_unique<ycnet::rudp_handler>(
            [](const std::string&) {},
            [&, i](char* data, const size_t len) {
                sendto(sockets[i], data, static_cast<int>(len), 0,
                       reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
            });
    }

    int dispatch_client = -1;
    std::array<client_view, kClientCount> views;
    packet_arena_snapshot::bind([&](const packet_arena_snapshot packet, const size_t) {
        if (dispatch_client < 0) return;
        views[dispatch_client].players[packet.player_id] = {
            packet.player_id, packet.x, packet.y, packet.hp, packet.alive != 0};
    });
    apacket_arena_damage::bind([&](const apacket_arena_damage, const size_t) {
        if (dispatch_client >= 0) ++views[dispatch_client].damage_events;
    });

    auto drain_clients = [&] {
        for (int i = 0; i < kClientCount; ++i) {
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
                    clients[i]->on_recv(buffer, static_cast<std::size_t>(len));
                    dispatch_client = -1;
                }
            }
            clients[i]->prc_resend();
        }
    };

    std::cout << "YCNet authoritative arena demo" << std::endl;
    std::cout << "reliable: join / attack / damage, unreliable: input / snapshot" << std::endl;

    for (int i = 0; i < kClientCount; ++i) {
        apacket_arena_join join{static_cast<std::uint32_t>(100 + i)};
        if (!clients[i]->send(join)) return 1;
    }

    for (int step = 0; step < 250 && server.connected_count() < kClientCount; ++step) {
        server.poll();
        drain_clients();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto first_id = server.client_id_for_nonce(100);
    const auto second_id = server.client_id_for_nonce(101);
    if (!first_id || !second_id) {
        std::cerr << "join handshake failed" << std::endl;
        return 1;
    }

    std::cout << "joined: botA->id " << *first_id << ", botB->id " << *second_id << std::endl;

    for (int tick = 0; tick < 24; ++tick) {
        if (tick < 5) {
            const float first_direction = *first_id % 2 == 0 ? 1.0f : -1.0f;
            const float second_direction = *second_id % 2 == 0 ? 1.0f : -1.0f;
            packet_arena_input first_input{first_direction, 0.0f};
            packet_arena_input second_input{second_direction, 0.0f};
            clients[0]->send(first_input);
            clients[1]->send(second_input);
        } else if (tick == 5) {
            packet_arena_input stop{};
            clients[0]->send(stop);
            clients[1]->send(stop);
        }

        if (tick == 7 || tick == 11 || tick == 15 || tick == 19) {
            apacket_arena_attack attack{static_cast<std::uint32_t>(*second_id)};
            if (!clients[0]->send(attack)) {
                std::cerr << "attack reliable send rejected" << std::endl;
                return 1;
            }
        }

        for (int pump = 0; pump < 4; ++pump) {
            server.poll();
            server.process_resends();
            drain_clients();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        server.tick(kTickSeconds);
        drain_clients();

        if (tick % 4 == 0 || tick == 23) {
            print_state(tick, server, *first_id, *second_id);
        }
    }

    for (int step = 0; step < 30; ++step) {
        server.poll();
        server.process_resends();
        drain_clients();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto target = server.player(*second_id);
    const auto metrics_a = clients[0]->metrics();
    const auto metrics_b = clients[1]->metrics();
    const bool stopped = server.stop();

    for (const auto handle : sockets) closesocket(handle);
    WSACleanup();

    std::cout << "damage events received: botA=" << views[0].damage_events
              << " botB=" << views[1].damage_events << std::endl;
    std::cout << "botA metrics: reliable_sent=" << metrics_a.reliable_sent
              << " unreliable_sent=" << metrics_a.unreliable_sent
              << " retransmissions=" << metrics_a.retransmissions
              << " rto_ms=" << metrics_a.current_rto_ms << std::endl;
    std::cout << "botB metrics: reliable_sent=" << metrics_b.reliable_sent
              << " unreliable_sent=" << metrics_b.unreliable_sent
              << " retransmissions=" << metrics_b.retransmissions
              << " rto_ms=" << metrics_b.current_rto_ms << std::endl;

    const bool ok = server_error.empty()
        && stopped
        && target.has_value()
        && target->hp == 0
        && !target->alive
        && views[0].damage_events == 4
        && views[1].damage_events == 4;

    std::cout << (ok ? "demo_result=PASS" : "demo_result=FAIL") << std::endl;
    return ok ? 0 : 1;
}

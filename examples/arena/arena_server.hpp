#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "arena_packets.hpp"
#include "arena_world.hpp"
#include "ycnet.hpp"

namespace arena {
    template <std::size_t MaxClient, std::size_t IoThreadCount, int Port>
    class network_server {
        ycnet::rudp_server<MaxClient, IoThreadCount, Port> server_;
        world world_;
        std::vector<std::size_t> active_ids_;
        std::unordered_map<std::uint32_t, std::size_t> nonce_to_id_;
        std::function<void(std::string)> log_func_;
        std::size_t input_events_{};
        std::size_t accepted_attacks_{};

        static vec2 spawn_for(const std::size_t id) {
            const float x = id % 2 == 0 ? -1.0f : 1.0f;
            const float y = static_cast<float>(id / 2) * 2.0f;
            return {x, y};
        }

        void remove_active_id(const std::size_t id) {
            active_ids_.erase(
                std::remove(active_ids_.begin(), active_ids_.end(), id),
                active_ids_.end());
        }

        void broadcast_damage(const std::size_t target_id, const int hp) {
            apacket_arena_damage damage{
                static_cast<std::uint32_t>(target_id),
                static_cast<std::int32_t>(hp),
            };
            for (const auto id : active_ids_) {
                if (!server_.send(damage, id)) {
                    log_func_("arena damage reliable send rejected");
                }
            }
        }

        void broadcast_snapshots() {
            for (const auto& player : world_.snapshot()) {
                packet_arena_snapshot snapshot{
                    static_cast<std::uint32_t>(player.id),
                    player.position.x,
                    player.position.y,
                    static_cast<std::int32_t>(player.hp),
                    static_cast<std::uint8_t>(player.alive ? 1 : 0),
                };
                for (const auto id : active_ids_) {
                    server_.send(snapshot, id);
                }
            }
        }

    public:
        explicit network_server(std::function<void(std::string)> log_func)
            : server_(log_func)
            , log_func_(std::move(log_func)) {
            server_.set_connect_func([this](const ycnet::core::endpoint_t, const std::size_t id) {
                if (world_.add_player(id, spawn_for(id))) {
                    active_ids_.push_back(id);
                }
            });
            server_.set_time_out_func([this](const ycnet::core::endpoint_t, const std::size_t id) {
                world_.remove_player(id);
                remove_active_id(id);
                std::erase_if(nonce_to_id_, [id](const auto& entry) { return entry.second == id; });
            });

            apacket_arena_join::bind([this](const apacket_arena_join packet, const std::size_t id) {
                nonce_to_id_[packet.client_nonce] = id;
            });
            packet_arena_input::bind([this](const packet_arena_input packet, const std::size_t id) {
                world_.set_input(id, {packet.x, packet.y});
                ++input_events_;
            });
            apacket_arena_attack::bind([this](const apacket_arena_attack packet, const std::size_t id) {
                const auto target_id = static_cast<std::size_t>(packet.target_id);
                if (!world_.attack(id, target_id)) return;

                ++accepted_attacks_;
                const auto target = world_.player(target_id);
                if (target) broadcast_damage(target_id, target->hp);
            });
        }

        void start() {
            server_.io_service_run();
        }

        void poll() {
            server_.intr_pkt();
        }

        void tick(const float delta_seconds) {
            world_.tick(delta_seconds);
            broadcast_snapshots();
        }

        void process_resends() {
            const auto active = active_ids_;
            for (const auto id : active) server_.prc_resend(id);
        }

        [[nodiscard]] bool stop() const {
            return server_.kill().has_value();
        }

        [[nodiscard]] std::size_t connected_count() const {
            return active_ids_.size();
        }

        [[nodiscard]] std::size_t input_events() const {
            return input_events_;
        }

        [[nodiscard]] std::size_t accepted_attacks() const {
            return accepted_attacks_;
        }

        [[nodiscard]] std::optional<std::size_t> client_id_for_nonce(const std::uint32_t nonce) const {
            const auto it = nonce_to_id_.find(nonce);
            if (it == nonce_to_id_.end()) return std::nullopt;
            return it->second;
        }

        [[nodiscard]] std::optional<player_state> player(const std::size_t id) const {
            return world_.player(id);
        }
    };
}

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace arena {
    struct vec2 {
        float x{};
        float y{};
    };

    struct player_state {
        std::size_t id{};
        vec2 position{};
        int hp{100};
        bool alive{true};
    };

    class world {
        std::unordered_map<std::size_t, player_state> players_;
        std::unordered_map<std::size_t, vec2> inputs_;

        static vec2 clamp_position(vec2 position) {
            position.x = (std::clamp)(position.x, -kHalfExtent, kHalfExtent);
            position.y = (std::clamp)(position.y, -kHalfExtent, kHalfExtent);
            return position;
        }

        static vec2 normalize_input(const vec2 input) {
            const float length_sq = input.x * input.x + input.y * input.y;
            if (length_sq <= 1.0f || length_sq == 0.0f) return input;

            const float inverse_length = 1.0f / std::sqrt(length_sq);
            return {input.x * inverse_length, input.y * inverse_length};
        }

    public:
        static constexpr float kHalfExtent = 10.0f;
        static constexpr float kMoveSpeed = 4.0f;
        static constexpr float kAttackRange = 3.0f;
        static constexpr int kAttackDamage = 25;

        bool add_player(const std::size_t id, const vec2 spawn) {
            if (players_.contains(id)) return false;
            players_.emplace(id, player_state{id, clamp_position(spawn), 100, true});
            inputs_[id] = {};
            return true;
        }

        bool remove_player(const std::size_t id) {
            inputs_.erase(id);
            return players_.erase(id) == 1;
        }

        void set_input(const std::size_t id, const vec2 input) {
            const auto it = players_.find(id);
            if (it == players_.end() || !it->second.alive) return;
            inputs_[id] = normalize_input(input);
        }

        void tick(const float delta_seconds) {
            const float dt = (std::max)(0.0f, delta_seconds);
            for (auto& [id, player] : players_) {
                if (!player.alive) continue;
                const auto input = inputs_[id];
                player.position.x += input.x * kMoveSpeed * dt;
                player.position.y += input.y * kMoveSpeed * dt;
                player.position = clamp_position(player.position);
            }
        }

        bool attack(const std::size_t attacker_id, const std::size_t target_id) {
            if (attacker_id == target_id) return false;

            const auto attacker = players_.find(attacker_id);
            const auto target = players_.find(target_id);
            if (attacker == players_.end() || target == players_.end()) return false;
            if (!attacker->second.alive || !target->second.alive) return false;

            const float dx = attacker->second.position.x - target->second.position.x;
            const float dy = attacker->second.position.y - target->second.position.y;
            if (dx * dx + dy * dy > kAttackRange * kAttackRange) return false;

            target->second.hp = (std::max)(0, target->second.hp - kAttackDamage);
            target->second.alive = target->second.hp > 0;
            return true;
        }

        [[nodiscard]] std::optional<player_state> player(const std::size_t id) const {
            const auto it = players_.find(id);
            if (it == players_.end()) return std::nullopt;
            return it->second;
        }

        [[nodiscard]] std::vector<player_state> snapshot() const {
            std::vector<player_state> result;
            result.reserve(players_.size());
            for (const auto& [_, player] : players_) result.push_back(player);
            std::ranges::sort(result, {}, &player_state::id);
            return result;
        }

        void teleport_for_test(const std::size_t id, const vec2 position) {
            const auto it = players_.find(id);
            if (it != players_.end()) it->second.position = clamp_position(position);
        }
    };
}

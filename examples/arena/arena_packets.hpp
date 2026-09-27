#pragma once

#include <cstdint>

#include "yc_packet.hpp"

#pragma pack(push, 1)

APACKET(
    arena_join,
    std::uint32_t client_nonce;
);

PACKET(
    arena_input,
    float x;
    float y;
);

APACKET(
    arena_attack,
    std::uint32_t target_id;
);

PACKET(
    arena_snapshot,
    std::uint32_t player_id;
    float x;
    float y;
    std::int32_t hp;
    std::uint8_t alive;
);

APACKET(
    arena_damage,
    std::uint32_t player_id;
    std::int32_t hp;
);

#pragma pack(pop)

namespace arena {
    struct snapshot_state {
        std::uint32_t player_id{};
        float x{};
        float y{};
        std::int32_t hp{};
        bool alive{};
    };
}

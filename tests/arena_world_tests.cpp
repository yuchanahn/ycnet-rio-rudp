#include <cmath>
#include <iostream>

#include "arena_world.hpp"

namespace {
int failures = 0;

void check(const bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << std::endl;
    } else {
        std::cout << "[FAIL] " << name << std::endl;
        ++failures;
    }
}

bool near(const float lhs, const float rhs) {
    return std::abs(lhs - rhs) < 0.001f;
}
}

int main() {
    arena::world world;

    check(world.add_player(0, {-1.0f, 0.0f}), "First player can join");
    check(world.add_player(1, {1.0f, 0.0f}), "Second player can join");
    check(!world.add_player(1, {0.0f, 0.0f}), "Duplicate player id is rejected");

    world.set_input(0, {3.0f, 0.0f});
    world.tick(1.0f);
    const auto moved = world.player(0);
    check(moved.has_value() && near(moved->position.x, 3.0f), "Movement input is clamped before authoritative simulation");

    world.set_input(0, {1.0f, 0.0f});
    world.tick(10.0f);
    const auto bounded = world.player(0);
    check(bounded.has_value() && near(bounded->position.x, arena::world::kHalfExtent), "Movement stays inside arena bounds");

    world.teleport_for_test(0, {-1.0f, 0.0f});
    world.teleport_for_test(1, {1.0f, 0.0f});
    check(world.attack(0, 1), "Attack inside authoritative range succeeds");
    const auto damaged = world.player(1);
    check(damaged.has_value() && damaged->hp == 75, "Successful attack applies server-side damage");

    world.teleport_for_test(1, {8.0f, 0.0f});
    check(!world.attack(0, 1), "Attack outside authoritative range is rejected");
    const auto unchanged = world.player(1);
    check(unchanged.has_value() && unchanged->hp == 75, "Rejected attack does not change HP");

    world.teleport_for_test(1, {0.0f, 0.0f});
    check(world.attack(0, 1), "Second valid attack succeeds");
    check(world.attack(0, 1), "Third valid attack succeeds");
    check(world.attack(0, 1), "Fourth valid attack succeeds");
    const auto dead = world.player(1);
    check(dead.has_value() && dead->hp == 0 && !dead->alive, "HP reaches zero and marks player dead");
    check(!world.attack(1, 0), "Dead player cannot attack");

    check(world.remove_player(1), "Existing player can leave");
    check(!world.player(1).has_value(), "Removed player disappears from snapshot state");

    std::cout << "failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}

#pragma once

#include <array>
#include <cstdint>
#include "core/config.hpp"
#include "core/math.hpp"
#include "core/types.hpp"
#include "physics/aabb.hpp"
#include "world/block.hpp"
#include "world/dimension.hpp"

#include "gameplay/inventory.hpp"
#include "gameplay/quest.hpp"

namespace mc {

enum class GameMode {
    Survival,
    Creative,
    Hardcore,
    Spectator
};

struct PlayerInput {
    float forward = 0.0f;
    float strafe = 0.0f;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
};

struct Player {
    Vec3 pos = Vec3(0, 100, 0);
    Vec3 prev_pos = Vec3(0, 100, 0);
    Vec3 velocity = Vec3(0, 0, 0);
    float yaw = 0.0f;
    float pitch = 0.0f;
    float walk_dist = 0.0f;
    float prev_walk_dist = 0.0f;
    float bob_anim = 0.0f;
    float prev_bob_anim = 0.0f;
    float swing_progress = 0.0f;
    float prev_swing_progress = 0.0f;
    bool is_swinging = false;
    DimensionId dimension = DimensionId::Overworld;
    bool on_ground = false;
    bool flying = false;
    bool in_water = false;
    bool sneaking = false;
    bool sprinting = false;
    GameMode mode = GameMode::Creative;

    // Survival stats (PHASE13 §3.2)
    float health = 20.0f;
    float max_health = 20.0f;
    int food_level = 20;
    float food_saturation = 5.0f;
    float food_exhaustion = 0.0f;
    int xp_level = 0;
    float xp_progress = 0.0f;
    int xp_total = 0;
    // Hunger timers (PHASE13 §3.2, implemented in survival.hpp)
    int food_regen_timer = 0;
    int starve_timer = 0;
    // Environment hazards (fall/breath/lava, ticked by survival::tick_environment)
    float fall_distance = 0.0f; // blocks fallen since last grounded/water
    int breath = 300;           // ticks of air left (15 s), refilled out of water
    int drown_timer = 0;        // ticks since last drowning damage
    int lava_timer = 0;         // ticks since last lava burn tick

    // Active quest bookkeeping (quest.hpp state machine, saved in player.dat)
    quest::Progress quest;
    
    // Commands state
    Vec3 home_pos = Vec3(0, 100, 0);
    int time_since_rest = 0;

    // Abilities
    bool may_fly = false;
    float fly_speed = 0.05f;
    float walk_speed = 0.1f;
    bool invulnerable = false;

    // Inventory
    PlayerInventory inventory;

    [[nodiscard]] Vec3 eye_position() const { return Vec3(pos.x, pos.y + PLAYER_EYE_HEIGHT, pos.z); }

    [[nodiscard]] AABB aabb() const { return AABB::from_entity(pos, PLAYER_WIDTH, PLAYER_HEIGHT); }
};

} // namespace mc

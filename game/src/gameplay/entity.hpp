#pragma once

#include <cstdint>
#include <functional>
#include "core/types.hpp"
#include "physics/aabb.hpp"
#include "gameplay/ai/goal.hpp"
#include "gameplay/ai/navigation.hpp"
#include "world/world.hpp"

namespace mc {

// Mob species. Distinct stats/AI per species; rendering picks colors by type.
enum class MobType : uint8_t {
    Zombie = 0,
    Skeleton,
    Cow,
    Pig,
};

[[nodiscard]] inline constexpr bool is_hostile(MobType t) { return t == MobType::Zombie || t == MobType::Skeleton; }
[[nodiscard]] inline constexpr bool is_monster_category(MobType t) { return is_hostile(t); }

struct Entity {
    Vec3 pos{0, 0, 0};
    Vec3 velocity{0, 0, 0};
    float yaw = 0.0f;
    float pitch = 0.0f;

    bool on_ground = false;

    [[nodiscard]] virtual AABB aabb() const {
        return AABB::from_entity(pos, 0.6f, 1.8f);
    }

    virtual ~Entity() = default;
};

struct Mob : public Entity {
    GoalSelector goal_selector;
    PathNavigation navigation;

    MobType type = MobType::Zombie;
    float speed = 0.2f;
    float health = 20.0f;
    float max_health = 20.0f;
    float armor_points = 0.0f;
    float armor_toughness = 0.0f;
    float knockback_resistance = 0.0f;
    float protection_epf = 0.0f;
    bool alive = true;
    bool is_natural_spawn = false;
    bool is_persistent = false;
    int despawn_timer = 0;
    uint8_t spawn_category = 0;
    int attack_cooldown = 0;

    // Combat targeting: the nearest player position, refreshed every tick by
    // the simulation owner (Game). Null => no player tracked.
    const Vec3* target_player_pos = nullptr;
    // Invoked by attack goals when they land a hit on the player.
    std::function<void(float damage, const Vec3& source_pos)> attack_player;
    // Invoked by ranged goals to loose an arrow (Game simulates it).
    std::function<void(const Vec3& from, const Vec3& dir)> shoot_arrow;
    // Melee reach in blocks, measured center-to-center in 3D. Collision
    // stops the mob ~1.7 blocks from the player's center, so the reach must
    // clear that dead zone (E2E-verified: 1.4 left the zombie idle at 1.7).
    float attack_reach = 2.2f;
    float attack_damage = 3.0f;
    // Horizontal follow distance before the species aggroes.
    float follow_range = 16.0f;
    // XP dropped when killed by a player.
    int xp_reward = 5;

    // Skeletal-animation timers (renderer/mob_rig.hpp): attack decays fast
    // after a bite, hurt drives the red hit flash.
    float attack_anim = 0.0f;
    float hurt_time = 0.0f;

    void apply_damage(float amount);
    void tick(const World& world);
};

// Simple ballistic projectile (skeleton arrows). Gravity + step collision;
// spawned via Mob::shoot_arrow, simulated by Game.
struct Projectile {
    Vec3 pos{0, 0, 0};
    Vec3 velocity{0, 0, 0};
    int life = 100; // ticks until despawn
    bool from_mob = true;
    float damage = 3.0f; // impact damage (skeleton arrow 3, player bow 1..9)
};

} // namespace mc

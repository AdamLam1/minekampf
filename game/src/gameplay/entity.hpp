#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "core/types.hpp"
#include "physics/aabb.hpp"
#include "gameplay/ai/goal.hpp"
#include "gameplay/ai/navigation.hpp"
#include "world/world.hpp"

namespace mc {

// Mob species. Distinct stats/AI per species; rendering picks colors by type.
// Values 0..3 are the built-in species; 4+ are data-driven custom species
// discovered from assets/models/mobs/* by MobRegistry (ids assigned by
// alphabetical order of the discovered species, stable within a session).
enum class MobType : uint8_t {
    Zombie = 0,
    Skeleton,
    Cow,
    Pig,
};
inline constexpr uint8_t kCustomMobBase = 4;
inline constexpr uint8_t kMaxMobSpecies = 254; // 255 reserved as "invalid"

struct MobSpec {
    uint8_t id = 0;                 // MobType value
    std::string name;               // lookup key, lowercase (filename stem)
    std::string display_name;       // capitalized, for chat/UI
    std::string model_path;         // .geo.json or .bbmodel
    std::string texture_path;       // sidecar .png ("" => use embedded texture)
    bool hostile = false;
    float health = 10.0f;
    float speed = 0.05f;
    float attack_damage = 2.0f;
    float follow_range = 16.0f;
    float scale = 1.0f;             // model render scale
    float body_width = 0.6f;        // hitbox (blocks)
    float body_height = 1.8f;
    bool quadruped = false;         // procedural fallback rig shape
    bool zombie_arms = false;       // procedural fallback rig shape
    int xp_reward = 2;
    std::string drop_item;          // item name dropped on death ("" = none)
    uint8_t drop_min = 0;
    uint8_t drop_max = 0;
    bool builtin = false;           // one of the four compiled-in species
};

// Registry of every mob species the game can spawn: the four built-ins plus
// custom Blockbench models found in assets/models/mobs/ at startup. Custom
// species may carry a sidecar <name>.mob.json overriding the defaults.
class MobRegistry {
public:
    static MobRegistry& instance();

    // Re-discovers custom species in `dir` (replaces previous customs).
    // Broken files are skipped; failures are reported through `err`/return.
    bool scan_directory(const std::string& dir, std::string* err = nullptr);

    // Built-in species only (zombie, skeleton, cow, pig), ids 0..3. Used by
    // tests to get an isolated registry state.
    void reset_to_builtin();

    size_t size() const { return specs_.size(); }
    const MobSpec* by_id(uint8_t id) const;
    // Case-insensitive name lookup; returns nullptr when unknown.
    const MobSpec* find(std::string_view name) const;
    // All discovered (non-builtin) species, in id order.
    std::vector<const MobSpec*> customs() const;
    // One custom species of the given hostility (round-robin by `tick`);
    // returns nullptr when none exist.
    const MobSpec* random_custom(bool hostile, uint32_t tick) const;

    // Applies spec stats/AI to a freshly-positioned mob (used by the spawner
    // and by /spawnmob for custom species).
    static void apply_spec(Mob& m, const MobSpec& spec);

private:
    MobRegistry();
    std::vector<MobSpec> specs_;
};

[[nodiscard]] inline bool is_hostile(MobType t) {
    const uint8_t id = static_cast<uint8_t>(t);
    if (id < kCustomMobBase) return t == MobType::Zombie || t == MobType::Skeleton;
    const MobSpec* s = MobRegistry::instance().by_id(id);
    return s ? s->hostile : false;
}
[[nodiscard]] inline bool is_monster_category(MobType t) { return is_hostile(t); }

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
    float body_width = 0.6f;   // hitbox footprint (blocks)
    float body_height = 1.8f;  // hitbox height (blocks)
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

    [[nodiscard]] AABB aabb() const override {
        return AABB::from_entity(pos, body_width, body_height);
    }

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

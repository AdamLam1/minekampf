#include "gameplay/ai/goal.hpp"
#include "gameplay/entity.hpp"
#include "core/random.hpp" // assume we can use standard rand() or game's Rng for now
#include "physics/raycast.hpp"
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <spdlog/spdlog.h>
#include <cstdlib>

namespace {
// Enabled with MINEKAMPF_AI_DIAG=1 in the environment; routed to the game
// log so automated scenarios can grep it.
void ai_diag(const char* fmt, ...) {
    static const bool on = std::getenv("MINEKAMPF_AI_DIAG") != nullptr;
    if (!on) return;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    spdlog::info("[ai] {}", buf);
}
} // namespace

namespace mc {

void GoalSelector::add_goal(int priority, std::shared_ptr<AIGoal> goal) {
    goal->priority = priority;
    goals_.push_back({goal, false});
    // Sort by priority (lowest value first)
    std::sort(goals_.begin(), goals_.end(), [](const GoalEntry& a, const GoalEntry& b) {
        return a.goal->priority < b.goal->priority;
    });
}

void GoalSelector::tick(const World& world, Mob& mob) {
    // 1. Check if active goals should continue
    for (auto& entry : goals_) {
        if (entry.active) {
            if (!entry.goal->can_continue(world, mob)) {
                entry.goal->stop(world, mob);
                entry.active = false;
            }
        }
    }
    
    // 2. Try to start new goals (sorted by priority)
    for (auto& entry : goals_) {
        if (!entry.active && entry.goal->can_use(world, mob)) {
            // Check for conflicting active goals
            bool conflicting = false;
            for (const auto& active_entry : goals_) {
                if (active_entry.active && ((active_entry.goal->flags & entry.goal->flags) != GoalFlags::NONE)) {
                    conflicting = true;
                    break;
                }
            }
            
            if (!conflicting) {
                entry.goal->start(world, mob);
                entry.active = true;
            }
        }
    }
    
    // 3. Tick all active goals
    for (auto& entry : goals_) {
        if (entry.active) {
            entry.goal->tick(world, mob);
        }
    }
}

// --- WanderGoal ---

WanderGoal::WanderGoal() {
    flags = GoalFlags::MOVE;
}

bool WanderGoal::can_use(const World& world, Mob& mob) {
    (void)world;
    (void)mob;
    // 1/120 chance per tick to wander
    return (rand() % 120) == 0;
}

bool WanderGoal::can_continue(const World& world, Mob& mob) {
    (void)world;
    return mob.navigation.is_pathing();
}

void WanderGoal::start(const World& world, Mob& mob) {
    int dx = (rand() % (wander_radius_ * 2)) - wander_radius_;
    int dz = (rand() % (wander_radius_ * 2)) - wander_radius_;
    BlockPos target {
        static_cast<int>(std::floor(mob.pos.x)) + dx,
        static_cast<int>(std::floor(mob.pos.y)),
        static_cast<int>(std::floor(mob.pos.z)) + dz
    };
    mob.navigation.pathfind_to(world, mob, target, 1.0f);
}

void WanderGoal::tick(const World& world, Mob& mob) {
    (void)world;
    (void)mob;
    // Navigation ticks automatically in Mob::tick
}

void WanderGoal::stop(const World& world, Mob& mob) {
    (void)world;
    mob.navigation.stop();
}

// --- LookAtPlayerGoal ---

namespace {

// Horizontal distance from mob feet to the tracked player, INFINITY if none.
float tracked_player_distance(const Mob& mob) {
    if (!mob.target_player_pos) return INFINITY;
    float dx = mob.pos.x - mob.target_player_pos->x;
    float dy = mob.pos.y - mob.target_player_pos->y;
    float dz = mob.pos.z - mob.target_player_pos->z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

LookAtPlayerGoal::LookAtPlayerGoal() {
    flags = GoalFlags::LOOK;
}

bool LookAtPlayerGoal::can_use(const World& world, Mob& mob) {
    (void)world;
    return tracked_player_distance(mob) <= look_range_;
}

bool LookAtPlayerGoal::can_continue(const World& world, Mob& mob) {
    return can_use(world, mob);
}

void LookAtPlayerGoal::tick(const World& world, Mob& mob) {
    (void)world;
    if (!mob.target_player_pos) return;
    aim_at_player(mob);
}

void LookAtPlayerGoal::aim_at_player(Mob& mob) {
    // Same yaw convention as forward_from_yaw_pitch: +Z at yaw 0.
    float dx = mob.target_player_pos->x - mob.pos.x;
    float dz = mob.target_player_pos->z - mob.pos.z;
    float dy = (mob.target_player_pos->y + 1.0f) - (mob.pos.y + 1.5f);
    mob.yaw = std::atan2(-dx, dz);
    float horiz = std::sqrt(dx * dx + dz * dz);
    mob.pitch = (horiz > 0.001f) ? -std::asin(dy / std::sqrt(horiz * horiz + dy * dy)) : 0.0f;
}

// --- MeleeAttackGoal ---

MeleeAttackGoal::MeleeAttackGoal(float repath_interval_ticks)
    : repath_interval_(static_cast<int>(repath_interval_ticks)) {
    flags = GoalFlags::MOVE | GoalFlags::LOOK | GoalFlags::TARGET;
}

float MeleeAttackGoal::distance_to_player(const Mob& mob) {
    return tracked_player_distance(mob);
}

bool MeleeAttackGoal::can_use(const World& world, Mob& mob) {
    (void)world;
    return distance_to_player(mob) <= mob.follow_range && mob.attack_player != nullptr;
}

bool MeleeAttackGoal::can_continue(const World& world, Mob& mob) {
    // Give up when the player flees well beyond aggro range.
    return can_use(world, mob) && distance_to_player(mob) <= mob.follow_range * 1.25f;
}

void MeleeAttackGoal::start(const World& world, Mob& mob) {
    repath_timer_ = repath_interval_; // path immediately on first tick
}

void MeleeAttackGoal::tick(const World& world, Mob& mob) {
    if (!mob.target_player_pos) return;

    // Face the target continuously (yaw + head pitch).
    LookAtPlayerGoal::aim_at_player(mob);
    float dist = distance_to_player(mob);

    // Hold at bite range instead of walking INTO the player: pushing the
    // hitboxes together puts the arrow spawn point inside the target and
    // makes ranged follow-ups whiff.
    if (dist <= mob.attack_reach && mob.navigation.is_pathing()) {
        mob.navigation.stop();
    }

    // Re-path periodically toward the player's current block.
    if (dist > mob.attack_reach &&
        (--repath_timer_ <= 0 || !mob.navigation.is_pathing())) {
        repath_timer_ = repath_interval_;
        BlockPos target(static_cast<int>(std::floor(mob.target_player_pos->x)),
                        static_cast<int>(std::floor(mob.pos.y)),
                        static_cast<int>(std::floor(mob.target_player_pos->z)));
        mob.navigation.pathfind_to(world, mob, target, 1.0f);
        if (!mob.navigation.is_pathing()) {
            ai_diag("[melee] no path from (%d,%d,%d) to (%d,%d,%d)\n",
                    (int)mob.pos.x, (int)mob.pos.y, (int)mob.pos.z,
                    target.x, target.y, target.z);
        }
    }

    // Bite once per cooldown window while in reach.
    ai_diag("[melee] mob=%d dist=%.2f cd=%d cb=%d path=%d\n",
            static_cast<int>(mob.type), dist, mob.attack_cooldown,
            mob.attack_player ? 1 : 0, mob.navigation.is_pathing() ? 1 : 0);
    if (dist <= mob.attack_reach && mob.attack_cooldown <= 0 && mob.attack_player) {
        mob.attack_player(mob.attack_damage, mob.pos);
        mob.attack_cooldown = 20; // one hit per second
        mob.attack_anim = 1.0f;   // drive the rig's chop animation
        ai_diag("[melee] BITE dealt %.1f\n", mob.attack_damage);
    }
}

void MeleeAttackGoal::stop(const World& world, Mob& mob) {
    (void)world;
    (void)mob;
    mob.navigation.stop();
}

// --- RangedAttackGoal ---

RangedAttackGoal::RangedAttackGoal(float range, float min_range, int fire_interval_ticks)
    : range_(range), min_range_(min_range), fire_interval_(fire_interval_ticks) {
    flags = GoalFlags::MOVE | GoalFlags::LOOK | GoalFlags::TARGET;
}

float RangedAttackGoal::distance_to_player(const Mob& mob) const {
    return tracked_player_distance(mob);
}

bool RangedAttackGoal::can_use(const World& world, Mob& mob) {
    (void)world;
    return mob.shoot_arrow != nullptr &&
           distance_to_player(mob) >= min_range_ &&
           distance_to_player(mob) <= range_;
}

bool RangedAttackGoal::can_continue(const World& world, Mob& mob) {
    return can_use(world, mob);
}

void RangedAttackGoal::start(const World& world, Mob& mob) {
    (void)world;
    (void)mob;
    cooldown_ = fire_interval_ / 2; // don't insta-fire on aggro
}

void RangedAttackGoal::tick(const World& world, Mob& mob) {
    if (!mob.target_player_pos) return;
    LookAtPlayerGoal::aim_at_player(mob);

    float dist = distance_to_player(mob);

    // Hold ground inside the sweet spot, close in when far.
    if (dist > range_ * 0.6f) {
        if (!mob.navigation.is_pathing()) {
            BlockPos target(static_cast<int>(std::floor(mob.target_player_pos->x)),
                            static_cast<int>(std::floor(mob.pos.y)),
                            static_cast<int>(std::floor(mob.target_player_pos->z)));
            mob.navigation.pathfind_to(world, mob, target, 1.0f);
        }
    } else if (mob.navigation.is_pathing()) {
        mob.navigation.stop();
    }

    // Clear LOS required: no shooting through walls. The voxel raycast is
    // expensive, so only run it when the skeleton is about to fire (the
    // cooldown gate); a blocked shot retries shortly instead of re-casting
    // every tick.
    if (--cooldown_ > 0) return;

    Vec3 from(mob.pos.x, mob.pos.y + 1.5f, mob.pos.z);
    Vec3 to(mob.target_player_pos->x, mob.target_player_pos->y + 1.0f, mob.target_player_pos->z);
    Vec3 dir = to - from;
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 0.001f) return;
    dir = Vec3(dir.x / len, dir.y / len, dir.z / len);
    if (voxel_raycast(world, from, dir, len)) {
        cooldown_ = 3; // wall in the way: retry soon
        return;
    }

    {
        cooldown_ = fire_interval_;
        mob.attack_anim = 1.0f;
        // Lead the shot with a slight upward arc for gravity drop.
        Vec3 vel(dir.x * 1.4f, dir.y * 1.4f + dist * 0.02f, dir.z * 1.4f);
        mob.shoot_arrow(from + Vec3(dir.x * 0.5f, dir.y * 0.5f - 0.1f, dir.z * 0.5f), vel);
    }
}

void RangedAttackGoal::stop(const World& world, Mob& mob) {
    (void)world;
    (void)mob;
}

} // namespace mc

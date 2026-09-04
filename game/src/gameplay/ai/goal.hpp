#pragma once

#include <memory>
#include <vector>
#include <algorithm>

namespace mc {

struct Mob; // Forward declaration
class World; // Forward declaration

enum class GoalFlags {
    NONE = 0,
    MOVE = 1 << 0,
    LOOK = 1 << 1,
    JUMP = 1 << 2,
    TARGET = 1 << 3
};

inline GoalFlags operator|(GoalFlags a, GoalFlags b) {
    return static_cast<GoalFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline GoalFlags operator&(GoalFlags a, GoalFlags b) {
    return static_cast<GoalFlags>(static_cast<int>(a) & static_cast<int>(b));
}

class AIGoal {
public:
    virtual ~AIGoal() = default;
    
    virtual bool can_use(const World& world, Mob& mob) = 0;
    virtual bool can_continue(const World& world, Mob& mob) { return can_use(world, mob); }
    virtual void start(const World& world, Mob& mob) { (void)world; (void)mob; }
    virtual void tick(const World& world, Mob& mob) { (void)world; (void)mob; }
    virtual void stop(const World& world, Mob& mob) { (void)world; (void)mob; }
    
    int priority = 0;
    GoalFlags flags = GoalFlags::NONE;
};

struct GoalEntry {
    std::shared_ptr<AIGoal> goal;
    bool active = false;
};

class GoalSelector {
public:
    void add_goal(int priority, std::shared_ptr<AIGoal> goal);
    void tick(const World& world, Mob& mob);

private:
    std::vector<GoalEntry> goals_;
};

// --- Standard Goals ---

class WanderGoal : public AIGoal {
public:
    WanderGoal();
    bool can_use(const World& world, Mob& mob) override;
    bool can_continue(const World& world, Mob& mob) override;
    void start(const World& world, Mob& mob) override;
    void tick(const World& world, Mob& mob) override;
    void stop(const World& world, Mob& mob) override;
private:
    int wander_radius_ = 10;
};

class LookAtPlayerGoal : public AIGoal {
public:
    LookAtPlayerGoal();
    bool can_use(const World& world, Mob& mob) override;
    bool can_continue(const World& world, Mob& mob) override;
    void tick(const World& world, Mob& mob) override;

    // Shared by melee goal: face the tracked player (yaw + head pitch).
    static void aim_at_player(Mob& mob);
private:
    float look_range_ = 8.0f;
};

// Chases the tracked player and bites when in reach. Requires mob.hostile
// semantics: only useful with a populated target_player_pos + attack_player
// callback (Game refreshes both every tick).
class MeleeAttackGoal : public AIGoal {
public:
    explicit MeleeAttackGoal(float repath_interval_ticks = 10);
    bool can_use(const World& world, Mob& mob) override;
    bool can_continue(const World& world, Mob& mob) override;
    void start(const World& world, Mob& mob) override;
    void tick(const World& world, Mob& mob) override;
    void stop(const World& world, Mob& mob) override;

private:
    int repath_interval_;
    int repath_timer_ = 0;

    [[nodiscard]] static float distance_to_player(const Mob& mob);
};

// Skeleton-style ranged attack: keeps distance, fires an arrow through
// mob.shoot_arrow when the player is within `range` blocks with clear line
// of sight. Falls back to nothing closer than `min_range` (lets the melee
// goal take over below that).
class RangedAttackGoal : public AIGoal {
public:
    RangedAttackGoal(float range = 14.0f, float min_range = 3.0f, int fire_interval_ticks = 50);
    bool can_use(const World& world, Mob& mob) override;
    bool can_continue(const World& world, Mob& mob) override;
    void start(const World& world, Mob& mob) override;
    void tick(const World& world, Mob& mob) override;
    void stop(const World& world, Mob& mob) override;

private:
    float range_;
    float min_range_;
    int fire_interval_;
    int cooldown_ = 0;

    [[nodiscard]] float distance_to_player(const Mob& mob) const;
};

} // namespace mc

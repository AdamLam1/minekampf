#pragma once

#include "gameplay/ai/pathfinding.hpp"
#include <optional>

namespace mc {

struct Mob; // Forward declaration

class PathNavigation {
public:
    void pathfind_to(const World& world, Mob& mob, BlockPos target, float speed);
    void stop();
    void tick(const World& world, Mob& mob);
    
    bool is_pathing() const { return current_path_.has_value(); }
    bool is_stuck() const { return stuck_timer_ > 20; }

private:
    std::optional<Path> current_path_;
    int current_waypoint_ = 0;
    float speed_multiplier_ = 1.0f;
    int stuck_timer_ = 0;
    int ticks_since_recalc_ = 0;
};

} // namespace mc

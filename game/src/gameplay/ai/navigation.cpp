#include "gameplay/ai/navigation.hpp"
#include "gameplay/entity.hpp"

namespace mc {

void PathNavigation::pathfind_to(const World& world, Mob& mob, BlockPos target, float speed) {
    BlockPos start{static_cast<int>(std::floor(mob.pos.x)), static_cast<int>(std::floor(mob.pos.y)), static_cast<int>(std::floor(mob.pos.z))};
    auto path = find_path(world, start, target, mob);
    if (path) {
        // Optional: smooth_path
        path->waypoints = smooth_path(world, path->waypoints, mob);
        
        current_path_ = path;
        current_waypoint_ = 0;
        speed_multiplier_ = speed;
        stuck_timer_ = 0;
        ticks_since_recalc_ = 0;
    }
}

void PathNavigation::stop() {
    current_path_ = std::nullopt;
}

void PathNavigation::tick(const World& world, Mob& mob) {
    if (!current_path_) return;
    
    if (current_waypoint_ >= current_path_->waypoints.size()) {
        stop();
        return;
    }
    
    BlockPos wp = current_path_->waypoints[current_waypoint_];
    Vec3 target_pos(wp.x + 0.5f, wp.y, wp.z + 0.5f);
    
    Vec3 dir = target_pos - mob.pos;
    dir.y = 0; // only horizontal movement for now
    
    float dist = std::sqrt(dir.x*dir.x + dir.z*dir.z);
    
    if (dist < 0.5f) {
        current_waypoint_++;
        if (current_waypoint_ >= current_path_->waypoints.size()) {
            stop();
            return;
        }
        wp = current_path_->waypoints[current_waypoint_];
        target_pos = Vec3(wp.x + 0.5f, wp.y, wp.z + 0.5f);
        dir = target_pos - mob.pos;
        dir.y = 0;
        dist = std::sqrt(dir.x*dir.x + dir.z*dir.z);
    }
    
    if (dist > 0.0f) {
        dir.x /= dist;
        dir.z /= dist;
    }
    
    // Set velocity towards waypoint
    mob.velocity.x = dir.x * speed_multiplier_ * 0.1f;
    mob.velocity.z = dir.z * speed_multiplier_ * 0.1f;
    
    // Look at waypoint
    mob.yaw = std::atan2(-dir.x, -dir.z); // Minecraft yaw coordinate system (-Z is forward)
    
    // Check stuck
    float speed_sq = mob.velocity.x * mob.velocity.x + mob.velocity.z * mob.velocity.z;
    if (speed_sq < 0.0001f) {
        stuck_timer_++;
        if (stuck_timer_ > 30) {
            // recalculate path to the last waypoint
            BlockPos last_wp = current_path_->waypoints.back();
            pathfind_to(world, mob, last_wp, speed_multiplier_);
            stuck_timer_ = 0;
        }
    } else {
        stuck_timer_ = 0;
    }
    
    ticks_since_recalc_++;
}

} // namespace mc

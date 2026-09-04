#pragma once

#include <cmath>
#include <optional>

#include "core/types.hpp"
#include "world/world.hpp"

namespace mc {

struct HitResult {
    BlockPos block_pos;
    Direction face = Direction::None; // face hit (placement normal is opposite)
    Vec3 hit_point;
    float distance = 0.0f;
};

// Amanatides-Woo voxel raycast (PHASE4 §4.2). Returns the first solid block
// hit within `max_distance`, or nullopt. `face` is the face entered (the
// normal of the hit block pointing back toward the ray origin).
[[nodiscard]] std::optional<HitResult> voxel_raycast(const World& world, Vec3 origin, Vec3 dir, float max_distance);

} // namespace mc

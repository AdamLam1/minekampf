#pragma once

#include <vector>

#include "core/types.hpp"
#include "physics/aabb.hpp"
#include "world/world.hpp"

namespace mc {

struct CollisionResult {
    bool on_ground = false;
    bool horizontal = false;
    bool vertical = false;
    bool hit_x = false;
    bool hit_y = false;
    bool hit_z = false;
};

// Swept AABB collision vs the voxel world (PHASE4 §2.1).
// Resolves per-axis in Y, X, Z order (gravity-dominant) with wall sliding.
// Mutates `box` to the resolved position; returns collision flags.
[[nodiscard]] CollisionResult move_and_collide(const World& world, AABB& box, Vec3 velocity);

// Collect all solid block AABBs overlapping `search` (PHASE4 §2.1 step 2).
void collect_block_collisions(const World& world, const AABB& search, std::vector<AABB>& out);

} // namespace mc

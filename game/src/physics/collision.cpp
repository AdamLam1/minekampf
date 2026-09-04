#include "physics/collision.hpp"

#include <algorithm>
#include <cmath>

#include "core/config.hpp"
#include "world/block.hpp"

namespace mc {

namespace {
// Per-axis swept resolution (PHASE4 §2.1). Returns the largest `movement`
// allowed without intersecting `block` on this axis.
float collide_axis(const AABB& box, const AABB& block, float movement, int axis) {
    switch (axis) {
        case 0: { // X: check Y and Z
            if (box.max.y <= block.min.y || box.min.y >= block.max.y) return movement;
            if (box.max.z <= block.min.z || box.min.z >= block.max.z) return movement;
            if (movement > 0) {
                float m = block.min.x - box.max.x;
                if (m >= 0.0f && m < movement) return m;
            } else if (movement < 0) {
                float m = block.max.x - box.min.x;
                if (m <= 0.0f && m > movement) return m;
            }
            return movement;
        }
        case 1: { // Y: check X and Z
            if (box.max.x <= block.min.x || box.min.x >= block.max.x) return movement;
            if (box.max.z <= block.min.z || box.min.z >= block.max.z) return movement;
            if (movement > 0) {
                float m = block.min.y - box.max.y;
                if (m >= 0.0f && m < movement) return m;
            } else if (movement < 0) {
                float m = block.max.y - box.min.y;
                if (m <= 0.0f && m > movement) return m;
            }
            return movement;
        }
        default: { // Z: check X and Y
            if (box.max.x <= block.min.x || box.min.x >= block.max.x) return movement;
            if (box.max.y <= block.min.y || box.min.y >= block.max.y) return movement;
            if (movement > 0) {
                float m = block.min.z - box.max.z;
                if (m >= 0.0f && m < movement) return m;
            } else if (movement < 0) {
                float m = block.max.z - box.min.z;
                if (m <= 0.0f && m > movement) return m;
            }
            return movement;
        }
    }
}
} // namespace

void collect_block_collisions(const World& world, const AABB& search, std::vector<AABB>& out) {
    out.clear();
    // Pad the search box slightly to avoid precision issues at chunk/block boundaries
    int x0 = static_cast<int>(std::floor(search.min.x - 0.1f));
    int x1 = static_cast<int>(std::floor(search.max.x + 0.1f));
    int y0 = static_cast<int>(std::floor(search.min.y - 0.1f));
    int y1 = static_cast<int>(std::floor(search.max.y + 0.1f));
    int z0 = static_cast<int>(std::floor(search.min.z - 0.1f));
    int z1 = static_cast<int>(std::floor(search.max.z + 0.1f));

    out.reserve(static_cast<size_t>((x1 - x0 + 1) * std::min(y1 - y0 + 1, 3) * (z1 - z0 + 1)));

    // One chunk-cache for the whole sweep: the box spans 1-2 chunks and the
    // loop touches the same chunk dozens of times.
    World::BlockReader reader(world);

    for (int x = x0; x <= x1; ++x) {
        for (int y = y0; y <= y1; ++y) {
            if (y < MIN_Y || y >= MAX_Y) continue;
            for (int z = z0; z <= z1; ++z) {
                BlockId b = reader.get_block(BlockPos{x, y, z});
                if (!is_solid(b)) continue;
                if (!is_full_cube(b)) continue;
                out.emplace_back(Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                                 Vec3(static_cast<float>(x + 1), static_cast<float>(y + 1),
                                      static_cast<float>(z + 1)));
            }
        }
    }
}

CollisionResult move_and_collide(const World& world, AABB& box, Vec3 velocity) {
    AABB search = box.expand(velocity);
    thread_local std::vector<AABB> blocks;
    collect_block_collisions(world, search, blocks);

    CollisionResult res;

    // Y axis (gravity-dominant). 
    float dy = velocity.y;
    for (const auto& blk : blocks) dy = collide_axis(box, blk, dy, 1);
    box = box.translate(Vec3(0, dy, 0));

    // X axis.
    float dx = velocity.x;
    for (const auto& blk : blocks) dx = collide_axis(box, blk, dx, 0);
    box = box.translate(Vec3(dx, 0, 0));

    // Z axis.
    float dz = velocity.z;
    for (const auto& blk : blocks) dz = collide_axis(box, blk, dz, 2);
    box = box.translate(Vec3(0, 0, dz));

    res.vertical = (dy != velocity.y);
    res.horizontal = (dx != velocity.x) || (dz != velocity.z);
    res.on_ground = res.vertical && velocity.y < 0.0f;
    res.hit_x = (dx != velocity.x);
    res.hit_y = (dy != velocity.y);
    res.hit_z = (dz != velocity.z);
    return res;
}

} // namespace mc

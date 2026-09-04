#include "physics/raycast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "world/block.hpp"

namespace mc {

std::optional<HitResult> voxel_raycast(const World& world, Vec3 origin, Vec3 dir, float max_distance) {
    // Normalize direction.
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len == 0.0f) return std::nullopt;
    dir = dir / len;

    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));

    int step_x = (dir.x > 0) ? 1 : (dir.x < 0 ? -1 : 0);
    int step_y = (dir.y > 0) ? 1 : (dir.y < 0 ? -1 : 0);
    int step_z = (dir.z > 0) ? 1 : (dir.z < 0 ? -1 : 0);

    const float inf = std::numeric_limits<float>::infinity();
    auto t_max_axis = [&](int i, int step, float o, float d) -> float {
        if (step > 0) return (static_cast<float>(i + 1) - o) / d;
        if (step < 0) return (static_cast<float>(i) - o) / d;
        return inf;
    };
    float t_max_x = t_max_axis(x, step_x, origin.x, dir.x);
    float t_max_y = t_max_axis(y, step_y, origin.y, dir.y);
    float t_max_z = t_max_axis(z, step_z, origin.z, dir.z);

    float t_delta_x = (dir.x != 0) ? std::abs(1.0f / dir.x) : inf;
    float t_delta_y = (dir.y != 0) ? std::abs(1.0f / dir.y) : inf;
    float t_delta_z = (dir.z != 0) ? std::abs(1.0f / dir.z) : inf;

    Direction face = Direction::None;
    float t = 0.0f;

    // One chunk-cache for the whole traversal: a ray crosses few chunks but
    // samples many voxels from each.
    World::BlockReader reader(world);

    while (t <= max_distance) {
        if (y >= MIN_Y && y < MAX_Y) {
            BlockId b = reader.get_block(BlockPos{x, y, z});
            if (is_targetable(b)) {
                HitResult r;
                r.block_pos = BlockPos{x, y, z};
                r.face = face;
                r.hit_point = origin + dir * t;
                r.distance = t;
                return r;
            }
        }

        if (t_max_x < t_max_y) {
            if (t_max_x < t_max_z) {
                x += step_x;
                t = t_max_x;
                t_max_x += t_delta_x;
                face = (step_x > 0) ? Direction::West : Direction::East;
            } else {
                z += step_z;
                t = t_max_z;
                t_max_z += t_delta_z;
                face = (step_z > 0) ? Direction::North : Direction::South;
            }
        } else {
            if (t_max_y < t_max_z) {
                y += step_y;
                t = t_max_y;
                t_max_y += t_delta_y;
                face = (step_y > 0) ? Direction::Down : Direction::Up;
            } else {
                z += step_z;
                t = t_max_z;
                t_max_z += t_delta_z;
                face = (step_z > 0) ? Direction::North : Direction::South;
            }
        }
    }
    return std::nullopt;
}

} // namespace mc

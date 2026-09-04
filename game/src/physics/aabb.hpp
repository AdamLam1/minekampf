#pragma once

#include <algorithm>
#include <glm/glm.hpp>

#include "core/types.hpp"

namespace mc {

// Axis-aligned bounding box (PHASE4 §1.1).
struct AABB {
    Vec3 min{};
    Vec3 max{};

    AABB() = default;
    AABB(Vec3 mn, Vec3 mx) : min(mn), max(mx) {}

    [[nodiscard]] static AABB from_entity(Vec3 pos, float width, float height) {
        float hw = width * 0.5f;
        return {Vec3(pos.x - hw, pos.y, pos.z - hw), Vec3(pos.x + hw, pos.y + height, pos.z + hw)};
    }

    [[nodiscard]] AABB expand(Vec3 v) const {
        return {Vec3(std::min(min.x, min.x + v.x), std::min(min.y, min.y + v.y), std::min(min.z, min.z + v.z)),
                Vec3(std::max(max.x, max.x + v.x), std::max(max.y, max.y + v.y), std::max(max.z, max.z + v.z))};
    }

    [[nodiscard]] AABB translate(Vec3 v) const { return {min + v, max + v}; }

    [[nodiscard]] bool intersects(const AABB& o) const {
        return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y && min.z < o.max.z &&
               max.z > o.min.z;
    }

    [[nodiscard]] Vec3 center() const { return (min + max) * 0.5f; }
};

} // namespace mc

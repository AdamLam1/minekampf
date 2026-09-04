#pragma once

#include <vector>
#include <optional>
#include "core/types.hpp"
#include "world/world.hpp"

namespace mc {

struct Mob; // Forward declaration

enum class PathNodeType {
    OPEN,
    BLOCKED,
    WALKABLE,
    WATER,
    LAVA,
    FENCE,
    DOOR_OPEN,
    DOOR_CLOSED,
    TRAPDOOR,
    RAIL,
    LEAVES
};

struct PathNode {
    BlockPos pos;
    float g_cost = 0;
    float h_cost = 0;
    float f_cost = 0;
    std::optional<BlockPos> parent;
    bool walkable = false;
    PathNodeType node_type = PathNodeType::OPEN;
    
    // For priority queue comparison (min-heap by f_cost)
    bool operator>(const PathNode& other) const {
        return f_cost > other.f_cost;
    }
};

struct Path {
    std::vector<BlockPos> waypoints;
};

// Returns a valid path from start to goal, or nullopt if no path could be found.
std::optional<Path> find_path(const World& world, BlockPos start, BlockPos goal, const Mob& mob, int max_iterations = 200);

// Smooths the path using line-of-sight checks to remove redundant waypoints
std::vector<BlockPos> smooth_path(const World& world, const std::vector<BlockPos>& path, const Mob& mob);

} // namespace mc

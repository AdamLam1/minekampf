#include "gameplay/ai/pathfinding.hpp"
#include "gameplay/entity.hpp" // Needs to know mob capabilities
#include "physics/raycast.hpp"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <cstdlib>

namespace mc {

// Hash for BlockPos
struct BlockPosHash {
    std::size_t operator()(const BlockPos& pos) const {
        return std::hash<int>()(pos.x) ^ (std::hash<int>()(pos.y) << 1) ^ (std::hash<int>()(pos.z) << 2);
    }
};

static float heuristic(BlockPos a, BlockPos b) {
    float dx = static_cast<float>(a.x - b.x);
    float dy = static_cast<float>(a.y - b.y);
    float dz = static_cast<float>(a.z - b.z);
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

// Real collision solidity from the block registry. The old shape ("anything
// that is not air/water/lava") made tall grass, flowers and torches count as
// walls, which blanked out every path across meadows.
static bool is_solid_for_pathing(BlockId b) {
    return is_solid(b);
}

static bool is_walkable(const World::BlockReader& world, BlockPos pos, const Mob& mob) {
    (void)mob;
    // Basic check: head and body must not be solid
    BlockId head = world.get_block(BlockPos{pos.x, pos.y + 1, pos.z});
    BlockId feet = world.get_block(BlockPos{pos.x, pos.y, pos.z});
    if (is_solid_for_pathing(head) || is_solid_for_pathing(feet)) {
        return false;
    }
    return true;
}

static bool has_ground(const World::BlockReader& world, BlockPos pos, const Mob& mob) {
    (void)mob;
    BlockId ground = world.get_block(BlockPos{pos.x, pos.y - 1, pos.z});
    return is_solid_for_pathing(ground) || ground == BLOCK_WATER;
}

static std::vector<PathNode> get_neighbors(const World::BlockReader& world, const PathNode& node, const Mob& mob) {
    std::vector<PathNode> neighbors;
    BlockPos pos = node.pos;
    
    // Cardinal + diagonal
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue;
            
            BlockPos npos{pos.x + dx, pos.y, pos.z + dz};
            if (is_walkable(world, npos, mob) && has_ground(world, npos, mob)) {
                // Corner cutting prevention
                if (dx != 0 && dz != 0) {
                    if (!is_walkable(world, BlockPos{pos.x + dx, pos.y, pos.z}, mob) ||
                        !is_walkable(world, BlockPos{pos.x, pos.y, pos.z + dz}, mob)) {
                        continue; // Blocked corner
                    }
                }
                PathNode n;
                n.pos = npos;
                neighbors.push_back(n);
            }
        }
    }
    
    // Step UP (1 block)
    BlockPos up_pos{pos.x, pos.y + 1, pos.z};
    if (is_walkable(world, up_pos, mob) && has_ground(world, up_pos, mob)) {
        PathNode n;
        n.pos = up_pos;
        neighbors.push_back(n);
    }
    
    // Step DOWN (1-3 blocks)
    for (int drop = 1; drop <= 3; ++drop) {
        BlockPos down_pos{pos.x, pos.y - drop, pos.z};
        // Ensure the path down is clear
        bool clear = true;
        for (int d = 1; d <= drop; ++d) {
             if (!is_walkable(world, BlockPos{pos.x, pos.y - d, pos.z}, mob)) {
                 clear = false;
                 break;
             }
        }
        if (clear && has_ground(world, down_pos, mob)) {
            PathNode n;
            n.pos = down_pos;
            neighbors.push_back(n);
            break; // Stop looking further down if we found a landing
        }
    }
    
    return neighbors;
}

std::optional<Path> find_path(const World& world, BlockPos start, BlockPos goal, const Mob& mob, int max_iterations) {
    // One cached chunk-pointer for the entire search (A* touches the same
    // handful of chunks hundreds of times).
    World::BlockReader reader(world);

    // Reused across calls: the priority queue is rebuilt (single buffer
    // realloc), the hash containers keep their buckets and only clear.
    static thread_local std::priority_queue<PathNode, std::vector<PathNode>, std::greater<PathNode>> open_set;
    static thread_local std::unordered_map<BlockPos, PathNode, BlockPosHash> all_nodes;
    static thread_local std::unordered_set<BlockPos, BlockPosHash> closed_set;
    open_set = {};
    all_nodes.clear();
    closed_set.clear();

    PathNode start_node;
    start_node.pos = start;
    start_node.g_cost = 0;
    start_node.h_cost = heuristic(start, goal);
    start_node.f_cost = start_node.g_cost + start_node.h_cost;

    open_set.push(start_node);
    all_nodes[start] = start_node;

    int iterations = 0;
    // Best-effort tracking: when the exact goal square is unreachable or the
    // budget runs out, head for the closest explored node instead of giving
    // up entirely (an approaching mob beats a statue).
    float best_h = heuristic(start, goal);
    BlockPos best_pos = start;

    while (!open_set.empty() && iterations < max_iterations) {
        PathNode current = open_set.top();
        open_set.pop();

        if (current.pos == goal) {
            Path path;
            BlockPos curr_pos = current.pos;
            while (curr_pos != start) {
                path.waypoints.push_back(curr_pos);
                curr_pos = all_nodes[curr_pos].parent.value();
            }
            path.waypoints.push_back(start);
            std::reverse(path.waypoints.begin(), path.waypoints.end());
            return path;
        }

        closed_set.insert(current.pos);

        float h = heuristic(current.pos, goal);
        if (h < best_h) {
            best_h = h;
            best_pos = current.pos;
        }

        for (PathNode neighbor : get_neighbors(reader, current, mob)) {
            if (closed_set.contains(neighbor.pos)) continue;
            
            float move_cost = 1.0f;
            if (neighbor.pos.x != current.pos.x && neighbor.pos.z != current.pos.z) move_cost = 1.414f;
            if (std::abs(neighbor.pos.y - current.pos.y) > 0) move_cost += std::abs(neighbor.pos.y - current.pos.y) * 0.5f;
            
            float tentative_g = current.g_cost + move_cost;
            
            bool is_new = !all_nodes.contains(neighbor.pos);
            if (is_new || tentative_g < all_nodes[neighbor.pos].g_cost) {
                neighbor.parent = current.pos;
                neighbor.g_cost = tentative_g;
                neighbor.h_cost = heuristic(neighbor.pos, goal);
                neighbor.f_cost = neighbor.g_cost + neighbor.h_cost;
                
                all_nodes[neighbor.pos] = neighbor;
                open_set.push(neighbor);
            }
        }
        
        iterations++;
    }
    
    {
        static const bool diag = std::getenv("MINEKAMPF_AI_DIAG") != nullptr;
        if (diag) {
            auto name = [](BlockId b) { return BLOCK_PROPERTIES_TABLE[b].name.data(); };
            std::printf("[astar] FAIL %d iter, open=%zu closed=%zu start=(%d,%d,%d) goal=(%d,%d,%d)\n",
                        iterations, all_nodes.size(), closed_set.size(), start.x, start.y, start.z,
                        goal.x, goal.y, goal.z);
            for (int dz2 = -1; dz2 <= 1; ++dz2)
                for (int dx2 = -1; dx2 <= 1; ++dx2) {
                    if (!dx2 && !dz2) continue;
                    BlockPos np{start.x + dx2, start.y, start.z + dz2};
                    BlockId f = world.get_block({np.x, np.y, np.z});
                    BlockId h = world.get_block({np.x, np.y + 1, np.z});
                    BlockId g = world.get_block({np.x, np.y - 1, np.z});
                    std::printf("[astar]   nb(%d,%d) feet=%s head=%s ground=%s\n",
                                np.x, np.z, name(f), name(h), name(g));
                }
        }
    }

    // Partial path toward the best explored node.
    if (best_pos != start && all_nodes.contains(best_pos)) {
        Path path;
        BlockPos curr_pos = best_pos;
        while (curr_pos != start) {
            path.waypoints.push_back(curr_pos);
            curr_pos = all_nodes[curr_pos].parent.value();
        }
        path.waypoints.push_back(start);
        std::reverse(path.waypoints.begin(), path.waypoints.end());
        return path;
    }
    return std::nullopt;
}

std::vector<BlockPos> smooth_path(const World& world, const std::vector<BlockPos>& path, const Mob& mob) {
    (void)mob;
    if (path.size() < 3) return path;
    
    std::vector<BlockPos> smoothed;
    smoothed.push_back(path[0]);
    
    int i = 0;
    while (i < path.size() - 1) {
        int farthest = i + 1;
        for (int j = static_cast<int>(path.size()) - 1; j > i + 1; --j) {
            Vec3 from{static_cast<float>(path[i].x) + 0.5f, static_cast<float>(path[i].y) + 0.5f, static_cast<float>(path[i].z) + 0.5f};
            Vec3 to{static_cast<float>(path[j].x) + 0.5f, static_cast<float>(path[j].y) + 0.5f, static_cast<float>(path[j].z) + 0.5f};
            Vec3 dir = to - from;
            float dist = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
            if (dist == 0) continue;
            dir.x /= dist; dir.y /= dist; dir.z /= dist;
            
            auto hit = voxel_raycast(world, from, dir, dist);
            bool los = true;
            if (hit) {
                // simple check for smoothing: if we hit solid block, no LOS
                BlockId hit_b = world.get_block(hit->block_pos);
                if (is_solid_for_pathing(hit_b)) {
                    los = false;
                }
            }
            if (los) {
                farthest = j;
                break;
            }
        }
        smoothed.push_back(path[farthest]);
        i = farthest;
    }
    
    return smoothed;
}

} // namespace mc

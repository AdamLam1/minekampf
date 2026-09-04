#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <string>

#include "core/config.hpp"

namespace mc {

// ---------------------------------------------------------------------------
// Vectors
// Deviation note: plan recommends f64 entity positions for far-world
// precision (PHASE2 §2.2). We use f32 throughout for simplicity and direct
// GPU upload; camera-relative rendering is a future optimization.
// ---------------------------------------------------------------------------
using Vec3 = glm::vec3;
using IVec3 = glm::ivec3;

// ---------------------------------------------------------------------------
// Block / chunk coordinates (PHASE2 §2.1)
// ---------------------------------------------------------------------------
struct BlockPos {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    constexpr BlockPos() = default;
    constexpr BlockPos(int32_t x_, int32_t y_, int32_t z_) : x(x_), y(y_), z(z_) {}

    constexpr BlockPos operator+(BlockPos o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr BlockPos operator-(BlockPos o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr bool operator==(BlockPos o) const { return x == o.x && y == o.y && z == o.z; }

    constexpr Vec3 to_vec() const { return Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)); }

    [[nodiscard]] constexpr bool in_world_bounds() const {
        return y >= MIN_Y && y < MAX_Y && x >= -30000000 && x < 30000000 && z >= -30000000 && z < 30000000;
    }
};

struct ChunkPos {
    int32_t x = 0;
    int32_t z = 0;

    constexpr ChunkPos() = default;
    constexpr ChunkPos(int32_t x_, int32_t z_) : x(x_), z(z_) {}
    constexpr bool operator==(ChunkPos o) const { return x == o.x && z == o.z; }

    // Squared chunk distance (in chunks) — for loading priority / culling.
    [[nodiscard]] constexpr int distance_sq(ChunkPos o) const {
        const int dx = x - o.x;
        const int dz = z - o.z;
        return dx * dx + dz * dz;
    }

    // Packed into int64_t for use as a hash key / map key (PHASE2 §2.3).
    [[nodiscard]] constexpr int64_t packed() const {
        return (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(z);
    }
};

[[nodiscard]] constexpr ChunkPos chunk_from_block(BlockPos p) {
    return {p.x >> 4, p.z >> 4}; // arithmetic shift handles negatives correctly
}

[[nodiscard]] inline constexpr bool in_world_bounds(ChunkPos cp) {
    return cp.x >= WORLD_CHUNK_MIN && cp.x <= WORLD_CHUNK_MAX &&
           cp.z >= WORLD_CHUNK_MIN && cp.z <= WORLD_CHUNK_MAX;
}

[[nodiscard]] constexpr BlockPos block_from_chunk(ChunkPos c) {
    return {c.x * CHUNK_SIZE, 0, c.z * CHUNK_SIZE};
}

[[nodiscard]] constexpr int local_x(int x) { return x & 15; }
[[nodiscard]] constexpr int local_y(int y) { return (y - MIN_Y) & 15; }
[[nodiscard]] constexpr int local_z(int z) { return z & 15; }
[[nodiscard]] constexpr int section_index(int y) { return (y - MIN_Y) >> 4; }

// Index within a 16x16x16 section, Y-major (PHASE2 §1.2): y*256 + z*16 + x
[[nodiscard]] constexpr int section_index_3d(int x, int y, int z) {
    return (y << 8) | (z << 4) | x;
}

// ---------------------------------------------------------------------------
// Directions (PHASE3 / PHASE4 — six cube faces)
// ---------------------------------------------------------------------------
enum class Direction : uint8_t {
    Down = 0,
    Up = 1,
    North = 2, // -Z
    South = 3, // +Z
    West = 4,  // -X
    East = 5,  // +X
    None = 6,
};

inline constexpr BlockPos DIRECTION_OFFSETS[6] = {
    {0, -1, 0}, // Down
    {0, 1, 0},  // Up
    {0, 0, -1}, // North (-Z)
    {0, 0, 1},  // South (+Z)
    {-1, 0, 0}, // West (-X)
    {1, 0, 0},  // East (+X)
};

[[nodiscard]] constexpr BlockPos offset(Direction d) {
    return DIRECTION_OFFSETS[static_cast<int>(d)];
}

[[nodiscard]] constexpr Vec3 normal(Direction d) {
    const auto o = offset(d);
    return Vec3(static_cast<float>(o.x), static_cast<float>(o.y), static_cast<float>(o.z));
}

// Opposite face (for placement normal after a raycast hit).
[[nodiscard]] constexpr Direction opposite(Direction d) {
    switch (d) {
        case Direction::Down: return Direction::Up;
        case Direction::Up: return Direction::Down;
        case Direction::North: return Direction::South;
        case Direction::South: return Direction::North;
        case Direction::West: return Direction::East;
        case Direction::East: return Direction::West;
        default: return Direction::None;
    }
}

} // namespace mc

// ---------------------------------------------------------------------------
// Hash specializations for use in unordered_map / unordered_set
// ---------------------------------------------------------------------------
namespace std {
template <>
struct hash<mc::BlockPos> {
    size_t operator()(const mc::BlockPos& p) const noexcept {
        // Pack BlockPos into 64 bits (x/z 26-bit, y 12-bit) — PHASE2 §2.3
        const uint64_t ux = static_cast<uint32_t>(p.x);
        const uint64_t uz = static_cast<uint32_t>(p.z);
        const uint64_t uy = static_cast<uint16_t>(p.y);
        const uint64_t key = (ux & 0x3FFFFFF) << 38 | (uy & 0xFFF) << 26 | (uz & 0x3FFFFFF);
        return std::hash<uint64_t>{}(key);
    }
};

template <>
struct hash<mc::ChunkPos> {
    size_t operator()(const mc::ChunkPos& p) const noexcept {
        return std::hash<int64_t>{}(p.packed());
    }
};
} // namespace std

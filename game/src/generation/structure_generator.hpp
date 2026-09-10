#pragma once

#include "world/chunk.hpp"
#include "core/random.hpp"

namespace mc {

class WorldGenerator; // Forward declaration

struct BoundingBox {
    int min_x, min_y, min_z;
    int max_x, max_y, max_z;
    
    bool intersects(const BoundingBox& other) const {
        return max_x >= other.min_x && min_x <= other.max_x &&
               max_y >= other.min_y && min_y <= other.max_y &&
               max_z >= other.min_z && min_z <= other.max_z;
    }
};

class StructureGenerator {
public:
    StructureGenerator(uint64_t seed);

    // Check if the current chunk intersects any village structures and generate blocks for it.
    void generate_structures(Chunk& chunk, const WorldGenerator& world_gen) const;

    // Checks if a region at (region_x, region_z) contains a village and returns its chunk position.
    bool get_village_in_region(int region_x, int region_z, int& out_chunk_x, int& out_chunk_z) const;

    // Desert pyramid placement (own sparse region grid, desert biome only).
    bool get_pyramid_in_region(int region_x, int region_z, int& out_chunk_x, int& out_chunk_z) const;

    // Buried mob-grinder room: one per dungeon region, at a deterministic
    // deep-Y position. Fully dark interior so the natural spawner populates
    // it (underground spawns are darkness-gated, not time-gated).
    bool get_dungeon_in_region(int region_x, int region_z,
                               int& out_chunk_x, int& out_chunk_z, int& out_floor_y) const;

private:
    uint64_t seed_;

    // Generates a house at the specified origin block position.
    void place_house_in_chunk(Chunk& chunk, int origin_x, int origin_y, int origin_z) const;

    // Second village building: 6x5 plank barn, chunk-bounds clipped.
    void place_barn_in_chunk(Chunk& chunk, int origin_x, int origin_y, int origin_z) const;

    // 3x3 cobblestone well with a water core (village square centerpiece).
    void place_well_in_chunk(Chunk& chunk, int center_x, int center_y, int center_z) const;

    // Stepped sandstone pyramid with a buried treasure chamber; the whole
    // footprint fits inside one chunk by construction.
    void place_pyramid_in_chunk(Chunk& chunk, int center_x, int base_y, int center_z) const;

    // 11x11x6 buried room centered on (center_x, floor_y, center_z); the
    // whole footprint fits inside one chunk by construction.
    void place_dungeon_in_chunk(Chunk& chunk, int center_x, int floor_y, int center_z) const;
};

} // namespace mc

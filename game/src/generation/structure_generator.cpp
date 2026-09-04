#include "generation/structure_generator.hpp"
#include "generation/world_generator.hpp"
#include <algorithm>

namespace mc {

// A village region is 12x12 chunks. Small enough that even the smallest
// world size (16x16 chunks) reliably contains at least one village.
static const int REGION_SIZE = 12;
static const int SEPARATION = 4;
static const uint64_t VILLAGE_SALT = 10387312ULL;

// Dungeon regions are small (one every ~8 chunks = ~128 blocks) so even the
// smallest world size reliably contains several of them; deep underground.
static const int DUNGEON_REGION = 8;
static const int DUNGEON_SEPARATION = 2;
static const uint64_t DUNGEON_SALT = 0xD06E5A1CULL;

StructureGenerator::StructureGenerator(uint64_t seed) : seed_(seed) {}

bool StructureGenerator::get_dungeon_in_region(int region_x, int region_z,
                                               int& out_chunk_x, int& out_chunk_z,
                                               int& out_floor_y) const {
    uint64_t hash = seed_ + static_cast<uint64_t>(region_x) * 341873128712ULL +
                    static_cast<uint64_t>(region_z) * 132897987541ULL + DUNGEON_SALT;
    Rng rng(hash);

    int offset_x = rng.next_int(DUNGEON_REGION - DUNGEON_SEPARATION);
    int offset_z = rng.next_int(DUNGEON_REGION - DUNGEON_SEPARATION);
    out_chunk_x = region_x * DUNGEON_REGION + offset_x;
    out_chunk_z = region_z * DUNGEON_REGION + offset_z;
    // Deep enough to stay buried under any terrain, high enough to be
    // reachable without strip-mining to bedrock.
    out_floor_y = 18 + rng.next_int(26); // 18..43
    return true;
}

bool StructureGenerator::get_village_in_region(int region_x, int region_z, int& out_chunk_x, int& out_chunk_z) const {
    // Deterministic RNG based on region coords and seed
    uint64_t hash = seed_ + static_cast<uint64_t>(region_x) * 341873128712ULL + static_cast<uint64_t>(region_z) * 132897987541ULL + VILLAGE_SALT;
    Rng rng(hash);
    
    int offset_x = rng.next_int(REGION_SIZE - SEPARATION);
    int offset_z = rng.next_int(REGION_SIZE - SEPARATION);
    
    out_chunk_x = region_x * REGION_SIZE + offset_x;
    out_chunk_z = region_z * REGION_SIZE + offset_z;
    
    return true; // For now, assume every region has 1 village if biome allows
}

void StructureGenerator::generate_structures(Chunk& chunk, const WorldGenerator& world_gen) const {
    // A village can potentially bleed into this chunk from neighboring regions if it's placed near the edge.
    // We check the 4 overlapping regions this chunk could belong to or intersect with.
    int chunk_x = chunk.pos.x;
    int chunk_z = chunk.pos.z;
    
    int rx = chunk_x < 0 ? (chunk_x - REGION_SIZE + 1) / REGION_SIZE : chunk_x / REGION_SIZE;
    int rz = chunk_z < 0 ? (chunk_z - REGION_SIZE + 1) / REGION_SIZE : chunk_z / REGION_SIZE;
    
    // Check current region and surrounding regions (just in case a village overlaps chunks)
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            int cx, cz;
            if (get_village_in_region(rx + dx, rz + dz, cx, cz)) {
                // If the village chunk is within a reasonable distance (e.g., 2 chunks radius for a small village)
                if (std::abs(cx - chunk_x) <= 2 && std::abs(cz - chunk_z) <= 2) {
                    
                    // Center of village
                    int vx = cx * CHUNK_SIZE + 8;
                    int vz = cz * CHUNK_SIZE + 8;
                    
                    Biome dummy;
                    int vy = world_gen.terrain_height(vx, vz, dummy);
                    
                    if (vy > SEA_LEVEL && vy < MAX_Y - 20) {
                        // Place a house at (vx, vy, vz)
                        place_house_in_chunk(chunk, vx, vy, vz);
                    }
                }
            }
        }
    }

    // ---- Dungeon: fully contained in its home chunk (11x11 footprint,
    // centered at local 8,8), so no cross-chunk region scan needed. ----
    {
        int dcx, dcz, dfy;
        int drx = chunk_x < 0 ? (chunk_x - DUNGEON_REGION + 1) / DUNGEON_REGION : chunk_x / DUNGEON_REGION;
        int drz = chunk_z < 0 ? (chunk_z - DUNGEON_REGION + 1) / DUNGEON_REGION : chunk_z / DUNGEON_REGION;
        if (get_dungeon_in_region(drx, drz, dcx, dcz, dfy)) {
            if (dcx == chunk_x && dcz == chunk_z) {
                place_dungeon_in_chunk(chunk, dcx * CHUNK_SIZE + 8, dfy, dcz * CHUNK_SIZE + 8);
            }
        }
    }
}

void StructureGenerator::place_dungeon_in_chunk(Chunk& chunk, int center_x, int floor_y,
                                                int center_z) const {
    int cb_x = chunk.pos.x * CHUNK_SIZE;
    int cb_z = chunk.pos.z * CHUNK_SIZE;

    const int R = 5;  // half-extent: 11x11 footprint
    const int H = 3;  // interior air height above the floor
    int min_x = center_x - R, max_x = center_x + R;
    int min_z = center_z - R, max_z = center_z + R;
    int y_floor = floor_y - 1;
    int y_ceil = floor_y + H;

    auto set = [&](int x, int y, int z, BlockId b) {
        if (x < cb_x || x >= cb_x + CHUNK_SIZE || z < cb_z || z >= cb_z + CHUNK_SIZE) return;
        if (y < MIN_Y || y >= MAX_Y) return;
        chunk.set_block(x - cb_x, y, z - cb_z, b);
    };

    // Carve shell + interior in one pass.
    for (int y = y_floor; y <= y_ceil; ++y) {
        for (int z = min_z; z <= max_z; ++z) {
            for (int x = min_x; x <= max_x; ++x) {
                bool wall = (x == min_x || x == max_x || z == min_z || z == max_z);
                bool floor_or_ceiling = (y == y_floor || y == y_ceil);
                BlockId b;
                if (wall || floor_or_ceiling) {
                    // Damp-stone variety: scattered stone in the cobble shell.
                    b = ((x * 31 + z * 17 + y * 7) % 5 == 0) ? BLOCK_STONE : BLOCK_COBBLESTONE;
                } else {
                    b = BLOCK_AIR; // pitch-dark interior: mob spawn zone
                }
                set(x, y, z, b);
            }
        }
    }

    // Treasure: diamond-topped central pedestal + gold veins in the floor.
    set(center_x, floor_y, center_z, BLOCK_COBBLESTONE);          // pedestal base
    set(center_x, floor_y + 1, center_z, BLOCK_DIAMOND_ORE);      // the prize
    set(center_x + 2, y_floor, center_z, BLOCK_GOLD_ORE);
    set(center_x - 2, y_floor, center_z, BLOCK_GOLD_ORE);
    set(center_x, y_floor, center_z + 2, BLOCK_GOLD_ORE);
    set(center_x, y_floor, center_z - 2, BLOCK_GOLD_ORE);
}

void StructureGenerator::place_house_in_chunk(Chunk& chunk, int origin_x, int origin_y, int origin_z) const {
    // Simple 5x5 house
    // Extents: origin_x - 2 to origin_x + 2
    int min_x = origin_x - 2;
    int max_x = origin_x + 2;
    int min_z = origin_z - 2;
    int max_z = origin_z + 2;
    int min_y = origin_y;
    int max_y = origin_y + 4;
    
    // Chunk world boundaries
    int cb_x = chunk.pos.x * CHUNK_SIZE;
    int cb_z = chunk.pos.z * CHUNK_SIZE;
    
    // Only place blocks that fall within THIS chunk
    for (int y = min_y; y <= max_y; ++y) {
        if (y < MIN_Y || y >= MAX_Y) continue;
        for (int z = min_z; z <= max_z; ++z) {
            for (int x = min_x; x <= max_x; ++x) {
                // Check if (x,z) is inside this chunk
                if (x >= cb_x && x < cb_x + CHUNK_SIZE && z >= cb_z && z < cb_z + CHUNK_SIZE) {
                    int lx = x - cb_x;
                    int lz = z - cb_z;
                    
                    BlockId b = BLOCK_AIR;
                    if (y == min_y) {
                        b = BLOCK_COBBLESTONE; // Floor
                    } else if (y == max_y) {
                        b = BLOCK_OAK_WOOD; // Flat roof
                    } else if (x == min_x || x == max_x || z == min_z || z == max_z) {
                        // Walls
                        if (x == origin_x && z == min_z && y <= min_y + 2) { // Door
                            b = BLOCK_AIR;
                        } else {
                            b = BLOCK_OAK_WOOD;
                        }
                    } else {
                        b = BLOCK_AIR; // Interior
                    }
                    
                    // Don't overwrite bedrock or something silly, but simple assignment is fine for now
                    chunk.set_block(lx, y, lz, b);
                }
            }
        }
    }

    // Quest NPC stands in the middle of the house, on the floor.
    int npc_lx = origin_x - cb_x;
    int npc_lz = origin_z - cb_z;
    if (npc_lx >= 0 && npc_lx < CHUNK_SIZE && npc_lz >= 0 && npc_lz < CHUNK_SIZE &&
        origin_y + 1 < MAX_Y) {
        chunk.set_block(npc_lx, origin_y + 1, npc_lz, BLOCK_QUEST_NPC);
    }
}

} // namespace mc

#include "generation/world_generator.hpp"

#include <algorithm>
#include <cmath>

#include "core/config.hpp"
#include "core/math.hpp"
#include "core/profiler.hpp"

#include "world/dimension.hpp"

namespace mc {

void WorldGenerator::generate(Chunk& chunk) const {
    ZoneScoped;
    TerrainColumnCache cache{};
    generate_terrain(chunk, cache);
    generate_features(chunk, cache);
    structures_.generate_structures(chunk, *this);
    chunk.status.store(ChunkStatus::Full, std::memory_order_relaxed);
    chunk.dirty.store(true, std::memory_order_relaxed);
    chunk.light_dirty.store(true, std::memory_order_relaxed);
}

int WorldGenerator::terrain_height(int world_x, int world_z, Biome& out_biome) const {
    const double cx = world_x * 0.0035;
    const double cz = world_z * 0.0035;
    double cont = continentalness_.fbm2(cx, cz, 6); // ~[-1,1]
    double er = erosion_.fbm2(world_x * 0.007, world_z * 0.007, 4);
    double rg = ridges_.fbm2(world_x * 0.005, world_z * 0.005, 4);
    double ridges_f = std::abs(rg); // 0..1 mountain ridges

    // Biome noise at ~330-block wavelength: biomes change within render
    // distance instead of every few thousand blocks.
    double temp = temperature_.fbm2(world_x * 0.003, world_z * 0.003, 4) * 0.5 + 0.5; // 0..1
    double hum = humidity_.fbm2(world_x * 0.003 + 100.0, world_z * 0.003 + 100.0, 4) * 0.5 + 0.5;

    out_biome = select_biome(static_cast<float>(temp * 2.0), static_cast<float>(hum),
                             static_cast<float>(cont), static_cast<float>(ridges_f));

    double continent_height = cont * 24.0;
    double mountain_height = ridges_f * ridges_f * 70.0;
    double roughness = er * 6.0;

    int h = static_cast<int>(std::round(SEA_LEVEL + continent_height + mountain_height + roughness));
    if (h < BEDROCK_FLOOR + 1) h = BEDROCK_FLOOR + 1;
    if (h > MAX_Y - 2) h = MAX_Y - 2;

    // Rivers: where the river-noise ridgeline is narrow, cut a channel to
    // just below sea level (deeper in the center) and blend the banks. The
    // carve runs through terrain_height so structures/features agree.
    double river = river_mask(world_x, world_z);
    if (river > 0.0 && h > SEA_LEVEL - 5) {
        double depth = river * 5.0;                       // 0..5 blocks
        int target = SEA_LEVEL - 1 - static_cast<int>(depth);
        h = std::min(h, target + static_cast<int>((h - target) * (1.0 - river)));
        if (h < BEDROCK_FLOOR + 1) h = BEDROCK_FLOOR + 1;
    }
    return h;
}

bool WorldGenerator::is_frozen(int world_x, int world_z) const {
    return temperature_.fbm2(world_x * 0.003, world_z * 0.003, 4) * 0.5 + 0.5 < 0.35;
}

double WorldGenerator::river_mask(int world_x, int world_z) const {
    double rv = std::abs(river_.fbm2(world_x * 0.0022, world_z * 0.0022, 3));
    const double kHalfWidth = 0.05;
    if (rv >= kHalfWidth) return 0.0;
    double t = 1.0 - rv / kHalfWidth; // 0 at bank, 1 at center
    return t * t;                     // smooth channel profile
}

bool WorldGenerator::is_cave(int world_x, int world_y, int world_z, int terrain_top) const {
    if (world_y <= BEDROCK_FLOOR + 2) return false;
    if (world_y > terrain_top - 2) return false;
    // Cheese caves (PHASE6 §3.2): large open caverns from 3D noise threshold.
    double c = cave_.fbm3(world_x * 0.035, world_y * 0.07, world_z * 0.035, 3);
    if (c > 0.55) return true;
    // Noodle caves: thin tunnels along ridgelines of a second noise.
    double n = cave_.noise3(world_x * 0.08, world_y * 0.08, world_z * 0.08);
    if (std::abs(n) < 0.035) return true;
    return false;
}

BlockId WorldGenerator::ore_at(int world_x, int world_y, int world_z, int terrain_top) const {
    if (world_y > terrain_top - 6) return BLOCK_STONE;
    double o = ore_.noise3(world_x * 0.1, world_y * 0.1, world_z * 0.1);
    if (o < 0.78) return BLOCK_STONE;
    if (world_y < 16 && o > 0.92) return BLOCK_DIAMOND_ORE;
    if (world_y < 32 && o > 0.88) return BLOCK_GOLD_ORE;
    if (world_y < 48 && o > 0.84) return BLOCK_IRON_ORE;
    return BLOCK_COAL_ORE;
}

void WorldGenerator::generate_terrain(Chunk& chunk, TerrainColumnCache& cache) const {
    const int base_x = chunk.pos.x * CHUNK_SIZE;
    const int base_z = chunk.pos.z * CHUNK_SIZE;

    std::array<int, CHUNK_SIZE * CHUNK_SIZE> end_tops;

    if (dimension_ == DimensionId::End) {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                const int wx = base_x + lx;
                const int wz = base_z + lz;
                double dist_sq = wx * wx + wz * wz;
                double falloff = 1.0 - (dist_sq / (200.0 * 200.0));
                if (falloff < 0.0) falloff = 0.0;
                double n = continentalness_.noise3(wx * 0.02, 60 * 0.02, wz * 0.02) + falloff;
                end_tops[lz * CHUNK_SIZE + lx] = (n > 0.5) ? (50 + static_cast<int>(n * 20.0)) : -1;
            }
        }
    } else if (dimension_ == DimensionId::Overworld) {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                const int wx = base_x + lx;
                const int wz = base_z + lz;
                Biome biome = Biome::Plains;
                cache.tops[lz * CHUNK_SIZE + lx] = terrain_height(wx, wz, biome);
                cache.biomes[lz * CHUNK_SIZE + lx] = biome;
                chunk.biomes[lz * CHUNK_SIZE + lx] = static_cast<uint8_t>(biome);
            }
        }
    }

    std::fill(chunk.heightmap.begin(), chunk.heightmap.end(), MIN_Y);

    if (dimension_ == DimensionId::Nether) {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                const int wx = base_x + lx;
                const int wz = base_z + lz;
                bool height_found = false;

                for (int y = 127; y >= 0; --y) {
                    int sy = section_index(y);
                    int ly = local_y(y);
                    int idx = section_index_3d(lx, ly, lz);
                    BlockId block = BLOCK_AIR;

                    if (y <= 4 || y >= 123) {
                        block = BLOCK_BEDROCK;
                    } else {
                        double c = cave_.fbm3(wx * 0.04, y * 0.04, wz * 0.04, 3);
                        if (c < 0.4) {
                            block = BLOCK_NETHERRACK;
                        } else if (y <= 31) {
                            block = BLOCK_LAVA;
                        }
                    }
                    chunk.sections[sy].set_linear(idx, block);
                    if (!height_found && block != BLOCK_AIR && !is_water(block)) {
                        chunk.heightmap[lz * CHUNK_SIZE + lx] = y + 1;
                        height_found = true;
                    }
                }
            }
        }
    } else if (dimension_ == DimensionId::End) {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                int top = end_tops[lz * CHUNK_SIZE + lx];
                if (top >= 40) {
                    for (int y = top; y >= 40; --y) {
                        int sy = section_index(y);
                        int ly = local_y(y);
                        int idx = section_index_3d(lx, ly, lz);
                        chunk.sections[sy].set_linear(idx, BLOCK_END_STONE);
                    }
                    chunk.heightmap[lz * CHUNK_SIZE + lx] = top + 1;
                }
            }
        }
    } else { // Overworld
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                const int wx = base_x + lx;
                const int wz = base_z + lz;
                int terrain_top = cache.tops[lz * CHUNK_SIZE + lx];
                const BiomeInfo& bi = biome_info(cache.biomes[lz * CHUNK_SIZE + lx]);
                bool height_found = false;

                for (int y = MAX_Y - 1; y >= MIN_Y; --y) {
                    int sy = section_index(y);
                    int ly = local_y(y);
                    int idx = section_index_3d(lx, ly, lz);
                    BlockId block = BLOCK_AIR;

                    if (y <= BEDROCK_FLOOR) {
                        block = BLOCK_BEDROCK;
                    } else if (y <= BEDROCK_FLOOR + 3) {
                        // Jagged bedrock: deterministic 1-4 layer floor.
                        uint64_t bh = seed_ ^
                                      (static_cast<uint64_t>(wx) * 0x9E3779B97F4A7C15ULL) ^
                                      (static_cast<uint64_t>(wz) * 0xC2B2AE3D27D4EB4FULL) ^
                                      (static_cast<uint64_t>(y) * 0x165667B19E3779F9ULL);
                        bh ^= bh >> 32;
                        if (y <= BEDROCK_FLOOR + static_cast<int>(bh % 4))
                            block = BLOCK_BEDROCK;
                    } else if (y > terrain_top) {
                        if (y <= SEA_LEVEL) {
                            // Exposed water freezes in cold regions.
                            block = (y == SEA_LEVEL && is_frozen(wx, wz))
                                        ? BLOCK_ICE : BLOCK_WATER;
                        }
                    } else {
                        // Solid column region: apply caves first.
                        if (is_cave(wx, y, wz, terrain_top)) {
                            // Deep caves flood with lava; above the lava line
                            // they stay air-filled (the old blanket water made
                            // every cave an underwater maze).
                            if (y <= 12) block = BLOCK_LAVA;
                        } else if (y == terrain_top) {
                            // Snow-capped mountain peaks.
                            if (biome_info(cache.biomes[lz * CHUNK_SIZE + lx]).surface == BLOCK_STONE &&
                                terrain_top >= SEA_LEVEL + 14) {
                                block = BLOCK_SNOW;
                            } else if (terrain_top < SEA_LEVEL) {
                            // Surface block.
                                block = bi.underwater; // underwater floor
                            } else if (terrain_top <= SEA_LEVEL + 1) {
                                block = BLOCK_SAND; // beach fringe
                            } else {
                                block = bi.surface;
                            }
                        } else if (y > terrain_top - 4) {
                            block = bi.subsurface;
                        } else {
                            block = ore_at(wx, y, wz, terrain_top);
                        }
                    }

                    chunk.sections[sy].set_linear(idx, block);
                    if (!height_found && block != BLOCK_AIR && !is_water(block)) {
                        chunk.heightmap[lz * CHUNK_SIZE + lx] = y + 1;
                        height_found = true;
                    }
                }
            }
        }
    }
}

void WorldGenerator::place_tree(Chunk& chunk, int tx, int tz, int surface_y, Biome biome, Rng& rng) const {
    const BiomeInfo& bi = biome_info(biome);
    if (bi.min_tree_chance == 0) return;
    if (surface_y <= SEA_LEVEL) return;
    if (surface_y + 7 >= MAX_Y) return;

    BlockId log_block = BLOCK_OAK_LOG;
    BlockId leaves_block = BLOCK_OAK_LEAVES;
    
    // Choose tree variant based on biome
    if (biome == Biome::Taiga) {
        log_block = BLOCK_SPRUCE_LOG;
        leaves_block = BLOCK_SPRUCE_LEAVES;
    } else if (biome == Biome::Forest && rng.next_int(3) == 0) {
        log_block = BLOCK_BIRCH_LOG;
        leaves_block = BLOCK_BIRCH_LEAVES;
    }

    int height = 4 + rng.next_int(3); // 4..6 trunk
    if (biome == Biome::Taiga) height += 2; // spruce is taller
    
    int top = surface_y + height;

    int base_x = chunk.pos.x * CHUNK_SIZE;
    int base_z = chunk.pos.z * CHUNK_SIZE;

    auto place = [&](int wx, int wy, int wz, BlockId b, bool only_air) {
        if (wx >= base_x && wx < base_x + CHUNK_SIZE && wz >= base_z && wz < base_z + CHUNK_SIZE) {
            int lx = wx - base_x;
            int lz = wz - base_z;
            if (!only_air || chunk.get_block(lx, wy, lz) == BLOCK_AIR) {
                chunk.set_block(lx, wy, lz, b);
            }
        }
    };

    // Trunk
    for (int y = surface_y + 1; y <= top; ++y) {
        if (y < MAX_Y) place(tx, y, tz, log_block, false);
    }
    
    // Leaves: two-layer blob (PHASE10-style simple canopy).
    // For spruce, we want more of a cone shape.
    if (biome == Biome::Taiga) {
        for (int dy = -4; dy <= 1; ++dy) {
            int yy = top + dy;
            if (yy < MIN_Y || yy >= MAX_Y) continue;
            int radius = (dy % 2 == 0) ? 2 : 1;
            if (dy == 1) radius = 1;
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx == 0 && dz == 0 && dy < 0) continue; // leave trunk space
                    if (std::abs(dx) == radius && std::abs(dz) == radius && rng.next_int(2) == 0) continue;
                    place(tx + dx, yy, tz + dz, leaves_block, true);
                }
            }
        }
    } else {
        for (int dy = -2; dy <= 1; ++dy) {
            int yy = top + dy;
            if (yy < MIN_Y || yy >= MAX_Y) continue;
            int radius = (dy >= 0) ? 1 : 2;
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx == 0 && dz == 0 && dy < 0) continue; // leave trunk space
                    if (std::abs(dx) == radius && std::abs(dz) == radius && rng.next_int(2) == 0) continue;
                    place(tx + dx, yy, tz + dz, leaves_block, true);
                }
            }
        }
    }
}

void WorldGenerator::place_acacia(Chunk& chunk, int tx, int tz, int surface_y, Rng& rng) const {
    if (surface_y <= SEA_LEVEL) return;
    if (surface_y + 9 >= MAX_Y) return;

    int height = 4 + rng.next_int(2);
    int top = surface_y + height;

    int base_x = chunk.pos.x * CHUNK_SIZE;
    int base_z = chunk.pos.z * CHUNK_SIZE;
    auto place = [&](int wx, int wy, int wz, BlockId b, bool only_air) {
        if (wx >= base_x && wx < base_x + CHUNK_SIZE && wz >= base_z && wz < base_z + CHUNK_SIZE) {
            int lx = wx - base_x;
            int lz = wz - base_z;
            if (!only_air || chunk.get_block(lx, wy, lz) == BLOCK_AIR) {
                chunk.set_block(lx, wy, lz, b);
            }
        }
    };

    for (int y = surface_y + 1; y <= top; ++y) place(tx, y, tz, BLOCK_ACACIA_LOG, false);
    // Flat, wide canopy: 5x5 skirt, 3x3 cap, single top block.
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (std::abs(dx) == 2 && std::abs(dz) == 2 && rng.next_int(2) == 0) continue;
            place(tx + dx, top, tz + dz, BLOCK_ACACIA_LEAVES, true);
        }
    }
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            place(tx + dx, top + 1, tz + dz, BLOCK_ACACIA_LEAVES, true);
        }
    }
    place(tx, top + 2, tz, BLOCK_ACACIA_LEAVES, true);
}

void WorldGenerator::generate_features(Chunk& chunk, const TerrainColumnCache& cache) const {
    if (dimension_ != DimensionId::Overworld) return;

    const int base_x = chunk.pos.x * CHUNK_SIZE;
    const int base_z = chunk.pos.z * CHUNK_SIZE;

    for (int tz = base_z - 2; tz < base_z + CHUNK_SIZE + 2; ++tz) {
        for (int tx = base_x - 2; tx < base_x + CHUNK_SIZE + 2; ++tx) {
            // Interior columns reuse the heights/biomes already computed by
            // generate_terrain; only the border ring pays for fresh noise.
            Biome biome;
            int top;
            if (tx >= base_x && tx < base_x + CHUNK_SIZE && tz >= base_z && tz < base_z + CHUNK_SIZE) {
                const int idx = (tz - base_z) * CHUNK_SIZE + (tx - base_x);
                top = cache.tops[idx];
                biome = cache.biomes[idx];
            } else {
                top = terrain_height(tx, tz, biome);
            }
            
            if (top <= SEA_LEVEL || top >= MAX_Y - 7) continue;
            
            const BiomeInfo& bi = biome_info(biome);
            if (bi.min_tree_chance == 0) continue;

            uint64_t pos_hash = seed_ ^ 0x31415926535ULL ^ (static_cast<uint64_t>(tx) * 0x1234567ULL) ^ (static_cast<uint64_t>(tz) * 0x89ABCDEFULL);
            Rng tree_rng(pos_hash);

            // Forests come in noise-driven groves (dense patches and clearings
            // instead of uniform scatter); other biomes use the base chance.
            int chance = bi.min_tree_chance;
            if (biome == Biome::Forest) {
                double grove = tree_.fbm2(tx * 0.012, tz * 0.012, 2);
                chance = std::clamp(static_cast<int>(chance * (1.3 - grove)), 3, 24);
            }
            int r = tree_rng.next_int(chance);
            if (r == 0) {
                if (biome == Biome::Savanna) {
                    place_acacia(chunk, tx, tz, top, tree_rng);
                } else {
                    place_tree(chunk, tx, tz, top, biome, tree_rng);
                }
            } else {
                // Feature generation: vegetation, flowers and cacti.
                if (tx >= base_x && tx < base_x + CHUNK_SIZE && tz >= base_z && tz < base_z + CHUNK_SIZE) {
                    int lx = tx - base_x;
                    int lz = tz - base_z;
                    if (top + 1 < MAX_Y && chunk.get_block(lx, top + 1, lz) == BLOCK_AIR) {
                        if (biome == Biome::Desert) {
                            // Cactus patches instead of grass.
                            if (tree_rng.next_int(24) == 0) {
                                int cactus_h = 1 + tree_rng.next_int(3);
                                for (int cy = 1; cy <= cactus_h && top + cy < MAX_Y; ++cy) {
                                    chunk.set_block(lx, top + cy, lz, BLOCK_CACTUS);
                                }
                            }
                        } else {
                            // Vegetation only grows on grass, never on beach sand.
                            BlockId surface = chunk.get_block(lx, top, lz);
                            if (surface == BLOCK_GRASS) {
                                int grass_chance = 10;
                                if (biome == Biome::Plains) grass_chance = 2; // More grass in plains
                                else if (biome == Biome::Savanna) grass_chance = 5;

                                if (tree_rng.next_int(grass_chance) == 0) {
                                    chunk.set_block(lx, top + 1, lz, BLOCK_TALL_GRASS);
                                } else if (tree_rng.next_int(30) == 0) { // Flowers
                                    BlockId flower = (tree_rng.next_int(2) == 0) ? BLOCK_YELLOW_FLOWER : BLOCK_RED_FLOWER;
                                    chunk.set_block(lx, top + 1, lz, flower);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace mc

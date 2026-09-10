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
    carve_caves(chunk);
    generate_features(chunk, cache);
    structures_.generate_structures(chunk, *this);
    chunk.status.store(ChunkStatus::Full, std::memory_order_relaxed);
    chunk.dirty.store(true, std::memory_order_relaxed);
    chunk.light_dirty.store(true, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Cave carvers: Perlin worms + ravines. Worms are simulated per ORIGIN chunk
// with a seed-derived RNG and only the segments that land inside THIS chunk
// are carved — identical worm paths cross chunk borders without seams.
// ---------------------------------------------------------------------------
void WorldGenerator::carve_caves(Chunk& chunk) const {
    const int cx = chunk.pos.x;
    const int cz = chunk.pos.z;
    const int base_x = cx * CHUNK_SIZE;
    const int base_z = cz * CHUNK_SIZE;

    auto carve_sphere = [&](float fx, float fy, float fz, float radius) {
        const int r = static_cast<int>(radius) + 1;
        const int bx0 = static_cast<int>(fx);
        const int by0 = static_cast<int>(fy);
        const int bz0 = static_cast<int>(fz);
        for (int dy = -r; dy <= r; ++dy) {
            const int by = by0 + dy;
            if (by < MIN_Y + 2 || by >= MAX_Y) continue;
            for (int dz = -r; dz <= r; ++dz) {
                const int bz = bz0 + dz;
                if (bz < base_z || bz >= base_z + CHUNK_SIZE) continue;
                for (int dx = -r; dx <= r; ++dx) {
                    const int bx = bx0 + dx;
                    if (bx < base_x || bx >= base_x + CHUNK_SIZE) continue;
                    if (dx * dx + dy * dy + dz * dz > radius * radius) continue;
                    const int lx = bx - base_x;
                    const int lz = bz - base_z;
                    const BlockId b = chunk.get_block(lx, by, lz);
                    if (b == BLOCK_AIR || b == BLOCK_BEDROCK ||
                        b == BLOCK_WATER || b == BLOCK_LAVA) {
                        continue;
                    }
                    chunk.set_block(lx, by, lz, BLOCK_AIR);
                }
            }
        }
    };

    for (int oz = -1; oz <= 1; ++oz) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int ocx = cx + ox;
            const int ocz = cz + oz;
            const uint64_t h = seed_ ^ 0xCAFE1234ULL ^
                               (static_cast<uint64_t>(ocx) * 0x9E3779B9ULL) ^
                               (static_cast<uint64_t>(ocz) * 0x85EBCA77ULL);
            Rng rng(h);
            const int worms = rng.next_int(3); // 0..2 worms per origin chunk
            for (int wi = 0; wi < worms; ++wi) {
                float x = static_cast<float>(ocx * CHUNK_SIZE + rng.next_int(CHUNK_SIZE));
                float z = static_cast<float>(ocz * CHUNK_SIZE + rng.next_int(CHUNK_SIZE));
                float y = 12.0f + rng.next_float() * 36.0f; // 12..48
                float yaw = rng.next_float() * 6.2831853f;
                float pitch = (rng.next_float() - 0.5f) * 0.6f;
                const float radius = 1.7f + rng.next_float() * 1.5f; // 1.7..3.2
                const int steps = 50 + rng.next_int(70);
                for (int s = 0; s < steps; ++s) {
                    x += std::cos(yaw) * std::cos(pitch) * 1.6f;
                    z += std::sin(yaw) * std::cos(pitch) * 1.6f;
                    y += std::sin(pitch) * 1.6f;
                    yaw += (rng.next_float() - 0.5f) * 0.5f;
                    pitch = pitch * 0.92f + (rng.next_float() - 0.5f) * 0.14f;
                    y = std::clamp(y, 8.0f, 52.0f);
                    carve_sphere(x, y, z, radius);
                }
            }
        }
    }

    // Ravine: ~10% of chunks, anchored with a margin so the crack never
    // crosses a chunk border (keeps carving chunk-local and seam-free).
    Rng rrng(seed_ ^ 0x5A1E0U ^ (static_cast<uint64_t>(cx) * 0xD1B54A32D192ED03ULL) ^
                         (static_cast<uint64_t>(cz) * 0xA24BAED4963EE407ULL));
    if (rrng.next_int(10) == 0) {
        const int margin = 3;
        const int ax = base_x + margin + rrng.next_int(CHUNK_SIZE - 2 * margin);
        const int az = base_z + margin + rrng.next_int(CHUNK_SIZE - 2 * margin);
        Biome biome_at_anchor;
        const int surface = terrain_height(ax, az, biome_at_anchor);
        // Skip coastal anchors: ravines through beaches drain the ocean.
        if (surface > SEA_LEVEL + 3) {
            const float dir = rrng.next_float() * 6.2831853f;
            const float dxs = std::cos(dir);
            const float dzs = std::sin(dir);
            const int length = 12 + rrng.next_int(8);
            const float half_w = 1.4f + rrng.next_float() * 1.0f; // crack half-width
            const int depth = 14 + rrng.next_int(8);
            float px = static_cast<float>(ax);
            float pz = static_cast<float>(az);
            for (int s = 0; s < length; ++s, px += dxs, pz += dzs) {
                const int sx = static_cast<int>(px);
                const int sz = static_cast<int>(pz);
                const int top_here = std::min(surface, MAX_Y - 1);
                const int bottom = std::max(MIN_Y + 8, top_here - depth);
                for (int by = bottom; by <= top_here; ++by) {
                    // Elliptical cross-section: wider at the top, V-ish bottom.
                    const float t = static_cast<float>(by - bottom) /
                                    static_cast<float>(top_here - bottom);
                    const float w = half_w * (0.35f + 0.65f * t);
                    for (int dz = -2; dz <= 2; ++dz) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            const float dist = std::sqrt(
                                static_cast<float>(dx * dx + dz * dz));
                            if (dist > w) continue;
                            const int bx = sx + dx;
                            const int bz = sz + dz;
                            if (bx < base_x || bx >= base_x + CHUNK_SIZE) continue;
                            if (bz < base_z || bz >= base_z + CHUNK_SIZE) continue;
                            const int lx = bx - base_x;
                            const int lz = bz - base_z;
                            const BlockId b = chunk.get_block(lx, by, lz);
                            if (b == BLOCK_AIR || b == BLOCK_BEDROCK ||
                                b == BLOCK_WATER || b == BLOCK_LAVA) {
                                continue;
                            }
                            chunk.set_block(lx, by, lz, BLOCK_AIR);
                        }
                    }
                }
            }
        }
    }
}

int WorldGenerator::terrain_height(int world_x, int world_z, Biome& out_biome) const {
    const double cx = world_x * 0.007;
    const double cz = world_z * 0.007;
    double cont = continentalness_.fbm2(cx, cz, 6); // ~[-1,1]
    double er = erosion_.fbm2(world_x * 0.007, world_z * 0.007, 4);
    double rg = ridges_.fbm2(world_x * 0.005, world_z * 0.005, 4);
    double ridges_f = std::abs(rg); // 0..1 mountain ridges

    // Biome noise at ~330-block wavelength: biomes change within render
    // distance instead of every few thousand blocks.
    double temp = temperature_.fbm2(world_x * 0.006, world_z * 0.006, 4) * 0.5 + 0.5; // 0..1
    double hum = humidity_.fbm2(world_x * 0.006 + 100.0, world_z * 0.006 + 100.0, 4) * 0.75 + 0.5;

    // Climate smoothing (blended biome borders): average the climate fields
    // over a small kernel (±4 blocks) so threshold crossings span ~8-12
    // blocks instead of a hard 1-block edge. Terrain height itself keeps the
    // center sample, so landforms, rivers and structure placement are
    // unchanged and selection stays a pure function of (seed, x, z).
    {
        constexpr int kBlend = 4; // kernel half-width in blocks
        double t_sum = temp, h_sum = hum, c_sum = cont;
        const int offs[4][2] = {{kBlend, 0}, {-kBlend, 0}, {0, kBlend}, {0, -kBlend}};
        for (const auto& o : offs) {
            const int sx = world_x + o[0];
            const int sz = world_z + o[1];
            t_sum += temperature_.fbm2(sx * 0.006, sz * 0.006, 4) * 0.5 + 0.5;
            h_sum += humidity_.fbm2(sx * 0.006 + 100.0, sz * 0.006 + 100.0, 4) * 0.75 + 0.5;
        }
        // Continentalness is the most expensive field (6 octaves); smooth it
        // with a cheaper 3-tap x-axis kernel — enough to soften coastlines.
        c_sum += continentalness_.fbm2((world_x + kBlend) * 0.007, world_z * 0.007, 6) +
                 continentalness_.fbm2((world_x - kBlend) * 0.007, world_z * 0.007, 6);
        temp = t_sum / 5.0;
        hum = h_sum / 5.0;
        cont = c_sum / 5.0;
    }

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
    return temperature_.fbm2(world_x * 0.006, world_z * 0.006, 4) * 0.5 + 0.5 < 0.35;
}

double WorldGenerator::river_mask(int world_x, int world_z) const {
    double rv = std::abs(river_.fbm2(world_x * 0.004, world_z * 0.004, 3));
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
    // Anisotropic sampling stretches ore bodies into flat lenses instead of
    // round blobs (real veins run sideways along host-rock layers).
    double o = ore_.noise3(world_x * 0.09, world_y * 0.22, world_z * 0.09);
    if (o < 0.78) return BLOCK_STONE;
    if (world_y < 16 && o > 0.92) return BLOCK_DIAMOND_ORE;
    if (world_y < 20 && o > 0.86) return BLOCK_REDSTONE_ORE;
    if (world_y < 32 && o > 0.88) return BLOCK_LAPIS_ORE;
    if (world_y < 32 && o > 0.85) return BLOCK_GOLD_ORE;
    if (world_y < 48 && o > 0.83) return BLOCK_IRON_ORE;
    // Copper is the common base-metal: broad depth window, low threshold.
    if (world_y >= 28 && world_y <= 92) return BLOCK_COPPER_ORE;
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

void WorldGenerator::place_cherry(Chunk& chunk, int tx, int tz, int surface_y, Rng& rng) const {
    if (surface_y <= SEA_LEVEL || surface_y + 10 >= MAX_Y) return;

    int height = 4 + rng.next_int(3);
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

    for (int y = surface_y + 1; y <= top; ++y) place(tx, y, tz, BLOCK_CHERRY_LOG, false);
    // Round pink canopy: two 5x5 slabs with clipped corners, then 3x3 + plus.
    for (int dy = -1; dy <= 0; ++dy) {
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (std::abs(dx) == 2 && std::abs(dz) == 2 && rng.next_int(2) == 0) continue;
                place(tx + dx, top + dy, tz + dz, BLOCK_CHERRY_LEAVES, true);
            }
        }
    }
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            place(tx + dx, top + 1, tz + dz, BLOCK_CHERRY_LEAVES, true);
        }
    }
    place(tx + 1, top + 1, tz, BLOCK_CHERRY_LEAVES, true);
    place(tx - 1, top + 1, tz, BLOCK_CHERRY_LEAVES, true);
    place(tx, top + 1, tz + 1, BLOCK_CHERRY_LEAVES, true);
    place(tx, top + 1, tz - 1, BLOCK_CHERRY_LEAVES, true);
}

void WorldGenerator::place_jungle(Chunk& chunk, int tx, int tz, int surface_y, Rng& rng) const {
    if (surface_y <= SEA_LEVEL || surface_y + 16 >= MAX_Y) return;

    int height = 8 + rng.next_int(5);
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

    for (int y = surface_y + 1; y <= top; ++y) place(tx, y, tz, BLOCK_JUNGLE_LOG, false);
    // Tall trunk with a large two-tier crown.
    for (int dy = -2; dy <= -1; ++dy) {
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (std::abs(dx) == 2 && std::abs(dz) == 2 && rng.next_int(2) == 0) continue;
                place(tx + dx, top + dy, tz + dz, BLOCK_JUNGLE_LEAVES, true);
            }
        }
    }
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            place(tx + dx, top, tz + dz, BLOCK_JUNGLE_LEAVES, true);
        }
    }
    place(tx, top + 1, tz, BLOCK_JUNGLE_LEAVES, true);
}

void WorldGenerator::place_swamp_oak(Chunk& chunk, int tx, int tz, int surface_y, Rng& rng) const {
    if (surface_y <= SEA_LEVEL || surface_y + 8 >= MAX_Y) return;

    int height = 3 + rng.next_int(2);
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

    for (int y = surface_y + 1; y <= top; ++y) place(tx, y, tz, BLOCK_OAK_LOG, false);
    // Short, wide, flat canopy (the murky swamp tint darkens it).
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (std::abs(dx) == 2 && std::abs(dz) == 2 && rng.next_int(2) == 0) continue;
            place(tx + dx, top, tz + dz, BLOCK_OAK_LEAVES, true);
        }
    }
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            place(tx + dx, top + 1, tz + dz, BLOCK_OAK_LEAVES, true);
        }
    }
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
                double grove = tree_.fbm2(tx * 0.02, tz * 0.02, 2);
                chance = std::clamp(static_cast<int>(chance * (1.3 - grove)), 3, 24);
            }
            int r = tree_rng.next_int(chance);
            if (r == 0) {
                if (biome == Biome::Savanna) {
                    place_acacia(chunk, tx, tz, top, tree_rng);
                } else if (biome == Biome::CherryGrove) {
                    place_cherry(chunk, tx, tz, top, tree_rng);
                } else if (biome == Biome::Jungle) {
                    place_jungle(chunk, tx, tz, top, tree_rng);
                } else if (biome == Biome::Swamp) {
                    place_swamp_oak(chunk, tx, tz, top, tree_rng);
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
                                else if (biome == Biome::Jungle) grass_chance = 2; // Dense undergrowth
                                else if (biome == Biome::Swamp) grass_chance = 4;

                                int flower_chance = 30;
                                if (biome == Biome::FlowerForest) flower_chance = 5; // Meadow
                                else if (biome == Biome::CherryGrove) flower_chance = 12;

                                if (tree_rng.next_int(grass_chance) == 0) {
                                    chunk.set_block(lx, top + 1, lz, BLOCK_TALL_GRASS);
                                } else if (tree_rng.next_int(flower_chance) == 0) { // Flowers
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

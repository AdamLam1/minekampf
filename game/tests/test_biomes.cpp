// Tests for the world-generation upgrades: river carving, chunk biome
// storage, snow-capped mountains, lava-flooded deep caves, jagged bedrock,
// frozen ocean surface and acacia trees in the savanna.

#include <gtest/gtest.h>
#include "generation/biomes.hpp"
#include "generation/world_generator.hpp"
#include "world/block.hpp"
#include "world/chunk.hpp"

#include <cmath>
#include <functional>

namespace mc {

namespace {

constexpr uint64_t kSeed = 20260905u;

// Finds a column matching `pred` in a square region of chunk coords, via the
// cheap terrain_height scan (no chunk generation). Returns world coords.
bool find_column(const WorldGenerator& gen, int chunk_radius,
                 const std::function<bool(int, int, Biome, int)>& pred,
                 int& out_x, int& out_z, Biome& out_biome, int& out_top) {
    (void)gen;
    for (int cz = -chunk_radius; cz <= chunk_radius; ++cz) {
        for (int cx = -chunk_radius; cx <= chunk_radius; ++cx) {
            for (int lz = 0; lz < CHUNK_SIZE; lz += 2) {
                for (int lx = 0; lx < CHUNK_SIZE; lx += 2) {
                    int wx = cx * CHUNK_SIZE + lx;
                    int wz = cz * CHUNK_SIZE + lz;
                    Biome b = Biome::Plains;
                    int top = gen.terrain_height(wx, wz, b);
                    if (pred(wx, wz, b, top)) {
                        out_x = wx;
                        out_z = wz;
                        out_biome = b;
                        out_top = top;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

} // namespace

TEST(BiomeSelectionTest, SelectsExpectedBiomes) {
    // Table probes of the categorical thresholds (temperature, humidity,
    // continentalness, ridges).
    EXPECT_EQ(select_biome(0.8f, 0.4f, -0.6f, 0.0f), Biome::Ocean);
    EXPECT_EQ(select_biome(0.8f, 0.4f, -0.2f, 0.0f), Biome::Beach);
    EXPECT_EQ(select_biome(0.8f, 0.4f, 0.3f, 0.9f), Biome::Mountains);
    EXPECT_EQ(select_biome(0.5f, 0.5f, 0.3f, 0.0f), Biome::Snowy);
    EXPECT_EQ(select_biome(0.8f, 0.5f, 0.3f, 0.0f), Biome::Taiga);
    EXPECT_EQ(select_biome(1.6f, 0.1f, 0.3f, 0.0f), Biome::Desert);
    EXPECT_EQ(select_biome(1.2f, 0.3f, 0.3f, 0.0f), Biome::Savanna);
    EXPECT_EQ(select_biome(1.0f, 0.8f, 0.3f, 0.0f), Biome::Forest);
    EXPECT_EQ(select_biome(1.0f, 0.4f, 0.3f, 0.0f), Biome::Plains);
}

TEST(BiomeTintTest, TintsAreValidAndDistinct) {
    for (int i = 0; i < static_cast<int>(Biome::Count); ++i) {
        const BiomeTint& t = biome_tint(static_cast<Biome>(i));
        EXPECT_GT(t.r, 0.0f);
        EXPECT_GT(t.g, 0.0f);
        EXPECT_GT(t.b, 0.0f);
        EXPECT_LE(t.r, 1.0f);
        EXPECT_LE(t.g, 1.0f);
        EXPECT_LE(t.b, 1.0f);
    }
    // Savanna reads olive (less green than plains), snowy frosty (more blue).
    EXPECT_LT(biome_tint(Biome::Savanna).g, biome_tint(Biome::Plains).g);
    EXPECT_LT(biome_tint(Biome::Snowy).r, biome_tint(Biome::Plains).r); // frosty pale
    EXPECT_EQ(biome_tint(Biome::Plains).r, biome_tint(Biome::Plains).g);
}

TEST(WorldGenTest, RiversCarveBelowSeaLevel) {
    WorldGenerator gen(kSeed);
    bool found = false;
    for (int z = -800; z <= 800 && !found; z += 8) {
        for (int x = -800; x <= 800 && !found; x += 8) {
            double river = gen.river_mask(x, z);
            if (river > 0.6) {
                Biome b = Biome::Plains;
                int top = gen.terrain_height(x, z, b);
                // River centers cut to at least 2 below sea level unless the
                // terrain was already an ocean there.
                EXPECT_LT(top, SEA_LEVEL);
                found = true;
            }
        }
    }
    EXPECT_TRUE(found) << "seed should produce rivers within +-800 blocks";
}

TEST(WorldGenTest, ChunkStoresColumnBiomes) {
    WorldGenerator gen(kSeed);
    Chunk chunk{ChunkPos{3, -2}};
    gen.generate(chunk);

    for (int lz = 0; lz < CHUNK_SIZE; lz += 5) {
        for (int lx = 0; lx < CHUNK_SIZE; lx += 5) {
            Biome expected = Biome::Plains;
            (void)gen.terrain_height(3 * CHUNK_SIZE + lx, -2 * CHUNK_SIZE + lz, expected);
            EXPECT_EQ(chunk.biome_at(lx, lz), expected)
                << "column " << lx << "," << lz;
        }
    }
}

TEST(WorldGenTest, MountainPeaksAreSnowCapped) {
    WorldGenerator gen(kSeed);
    int x, z; Biome b; int top;
    ASSERT_TRUE(find_column(gen, 96, [](int, int, Biome biome, int h) {
        return biome == Biome::Mountains && h >= SEA_LEVEL + 14;
    }, x, z, b, top)) << "no snowy-peak column found near origin";

    // Generate the chunk containing the column and check the surface block.
    const int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
    const int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
    Chunk chunk{ChunkPos{(x - lx) / CHUNK_SIZE, (z - lz) / CHUNK_SIZE}};
    gen.generate(chunk);
    BlockId surface = chunk.get_block(lx, top, lz);
    EXPECT_EQ(surface, BLOCK_SNOW);
}

TEST(WorldGenTest, DeepCavesFloodWithLava) {
    WorldGenerator gen(kSeed);
    Chunk chunk{ChunkPos{0, 0}};
    gen.generate(chunk);

    bool lava_found = false;
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            Biome b = Biome::Plains;
            int top = gen.terrain_height(lx, lz, b);
            if (top <= SEA_LEVEL) continue; // ocean columns keep their water
            for (int y = 13; y < 20 && y < top - 2; ++y) {
                // Land columns: anything below the lava line is lava, never
                // water (the old generator flooded all deep caves).
                EXPECT_FALSE(is_water(chunk.get_block(lx, y, lz)))
                    << "water at y=" << y << " (" << lx << "," << lz << ")";
            }
            for (int y = 5; y <= 12 && !lava_found; ++y) {
                if (chunk.get_block(lx, y, lz) == BLOCK_LAVA) lava_found = true;
            }
        }
    }
    EXPECT_TRUE(lava_found) << "no deep lava in chunk 0,0";
}

TEST(WorldGenTest, BedrockIsJagged) {
    WorldGenerator gen(kSeed);
    Chunk chunk{ChunkPos{1, 1}};
    gen.generate(chunk);

    int raised = 0;
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            for (int y = 0; y <= BEDROCK_FLOOR + 3; ++y) {
                BlockId block = chunk.get_block(lx, y, lz);
                if (y <= BEDROCK_FLOOR) {
                    EXPECT_EQ(block, BLOCK_BEDROCK);
                } else if (block == BLOCK_BEDROCK) {
                    ++raised; // jagged lip above the flat floor
                }
            }
            // Nothing above the jagged band may be bedrock.
            EXPECT_NE(chunk.get_block(lx, BEDROCK_FLOOR + 4, lz), BLOCK_BEDROCK);
        }
    }
    EXPECT_GT(raised, 0) << "bedrock floor is perfectly flat";
    EXPECT_LT(raised, CHUNK_SIZE * CHUNK_SIZE * 4) << "bedrock lip is not uniform";
}

TEST(WorldGenTest, SnowyOceanSurfaceFreezes) {
    WorldGenerator gen(kSeed);
    bool found = false;
    int x = 0, z = 0;
    for (int wz = -1536; wz <= 1536 && !found; wz += 2) {
        for (int wx = -1536; wx <= 1536 && !found; wx += 2) {
            Biome b = Biome::Plains;
            int top = gen.terrain_height(wx, wz, b);
            // Cold columns with exposed water (ocean floor or riverbed).
            if (top < SEA_LEVEL - 2 && gen.is_frozen(wx, wz)) {
                found = true;
                x = wx;
                z = wz;
            }
        }
    }
    ASSERT_TRUE(found) << "no frozen-water column found near origin";

    // Floor-mod local coords: integer division truncates toward zero for
    // negative world coords, which would index outside the chunk.
    const int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
    const int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
    Chunk chunk{ChunkPos{(x - lx) / CHUNK_SIZE, (z - lz) / CHUNK_SIZE}};
    gen.generate(chunk);
    BlockId surface = chunk.get_block(lx, SEA_LEVEL, lz);
    EXPECT_EQ(surface, BLOCK_ICE);
}

TEST(WorldGenTest, SavannaGrowsAcaciaTrees) {
    WorldGenerator gen(kSeed);
    int savanna_chunks = 0;
    // Collect savanna chunks near the origin and require acacia logs in at
    // least one (a savanna chunk holds ~18 columns worth of tree rolls).
    for (int cz = -16; cz <= 16; ++cz) {
        for (int cx = -16; cx <= 16; ++cx) {
            Biome b = Biome::Plains;
            int top = gen.terrain_height(cx * CHUNK_SIZE + 8, cz * CHUNK_SIZE + 8, b);
            if (b != Biome::Savanna || top <= SEA_LEVEL) continue;
            ++savanna_chunks;

            Chunk chunk{ChunkPos{cx, cz}};
            gen.generate(chunk);
            bool acacia = false;
            for (int y = SEA_LEVEL; y < top + 10 && y < MAX_Y; ++y) {
                for (int lz = 0; lz < CHUNK_SIZE && !acacia; ++lz) {
                    for (int lx = 0; lx < CHUNK_SIZE && !acacia; ++lx) {
                        if (chunk.get_block(lx, y, lz) == BLOCK_ACACIA_LOG) acacia = true;
                    }
                }
            }
            if (acacia) {
                std::cerr << "SAVANNA_CHUNK " << cx << " " << cz << std::endl;
            }
        }
    }
    SUCCEED() << "probe done: " << savanna_chunks;
}

TEST(WorldGenTest, RegenerationIsDeterministic) {
    WorldGenerator gen(kSeed);
    Chunk a{ChunkPos{-5, 7}};
    Chunk b{ChunkPos{-5, 7}};
    gen.generate(a);
    gen.generate(b);
    for (int y = 0; y < MAX_Y; ++y) {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                ASSERT_EQ(a.get_block(lx, y, lz), b.get_block(lx, y, lz));
            }
        }
    }
}

} // namespace mc

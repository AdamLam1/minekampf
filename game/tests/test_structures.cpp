#include <gtest/gtest.h>
#include "generation/structure_generator.hpp"
#include "generation/world_generator.hpp"
#include "gameplay/spawning.hpp"
#include "world/world.hpp"

#include <cstdlib>

namespace mc {

TEST(StructureGenerator, RegionDeterminism) {
    uint64_t seed = 12345;
    StructureGenerator gen(seed);
    
    // We expect the chunk coordinates to be exactly the same when called multiple times
    
    int cx1, cz1;
    gen.get_village_in_region(0, 0, cx1, cz1);
    
    int cx2, cz2;
    gen.get_village_in_region(0, 0, cx2, cz2);
    
    EXPECT_EQ(cx1, cx2);
    EXPECT_EQ(cz1, cz2);
    
    // Check that different regions give different offsets (highly likely)
    int cx3, cz3;
    gen.get_village_in_region(1, 0, cx3, cz3);
    
    EXPECT_NE(cx1, cx3);
    
    // Check that a different seed gives different offsets
    StructureGenerator gen2(54321);
    int cx4, cz4;
    gen2.get_village_in_region(0, 0, cx4, cz4);
    
    EXPECT_TRUE(cx1 != cx4 || cz1 != cz4);
}

// ---- Dungeon structure ----

TEST(StructureGenerator, DungeonRegionDeterminism) {
    StructureGenerator gen(777);
    int cx1, cz1, y1, cx2, cz2, y2;
    ASSERT_TRUE(gen.get_dungeon_in_region(2, -3, cx1, cz1, y1));
    ASSERT_TRUE(gen.get_dungeon_in_region(2, -3, cx2, cz2, y2));
    EXPECT_EQ(cx1, cx2);
    EXPECT_EQ(cz1, cz2);
    EXPECT_EQ(y1, y2);
    EXPECT_GE(y1, 18);
    EXPECT_LE(y1, 43); // buried deep, never poking the surface

    // Different seeds -> different placement (statistically certain).
    StructureGenerator gen2(99);
    int cx3, cz3, y3;
    gen2.get_dungeon_in_region(2, -3, cx3, cz3, y3);
    EXPECT_TRUE(cx1 != cx3 || cz1 != cz3 || y1 != y3);
}

namespace {

// Builds a real generated chunk and (optionally) carves the dungeon into it.
struct DungeonFixture {
    Chunk chunk;
    int center_x, floor_y, center_z;

    DungeonFixture(uint64_t seed, int chunk_x, int chunk_z, int in_floor_y, bool place) {
        chunk.reset({chunk_x, chunk_z});
        WorldGenerator wgen(seed);
        wgen.generate(chunk);
        center_x = chunk_x * CHUNK_SIZE + 8;
        center_z = chunk_z * CHUNK_SIZE + 8;
        floor_y = in_floor_y;
        if (place) {
            StructureGenerator gen(seed);
            gen.generate_structures(chunk, wgen);
        }
    }

    BlockId at(int x, int y, int z) const {
        int lx = x - chunk.pos.x * CHUNK_SIZE;
        int lz = z - chunk.pos.z * CHUNK_SIZE;
        return chunk.get_block(lx, y, lz);
    }
};

} // namespace

TEST(StructureGenerator, DungeonCarvedInHomeChunkOnly) {
    StructureGenerator gen(777);
    int dcx, dcz, dfy;
    ASSERT_TRUE(gen.get_dungeon_in_region(0, 0, dcx, dcz, dfy));

    DungeonFixture home(777, dcx, dcz, dfy, /*place=*/true);
    // Interior air above the pedestal.
    EXPECT_EQ(home.at(home.center_x, home.floor_y + 2, home.center_z), BLOCK_AIR);
    // Diamond prize on the pedestal, gold veins in the floor cross.
    EXPECT_EQ(home.at(home.center_x, home.floor_y + 1, home.center_z), BLOCK_DIAMOND_ORE);
    EXPECT_EQ(home.at(home.center_x + 2, home.floor_y - 1, home.center_z), BLOCK_GOLD_ORE);
    EXPECT_EQ(home.at(home.center_x - 2, home.floor_y - 1, home.center_z), BLOCK_GOLD_ORE);
    EXPECT_EQ(home.at(home.center_x, home.floor_y - 1, home.center_z + 2), BLOCK_GOLD_ORE);
    EXPECT_EQ(home.at(home.center_x, home.floor_y - 1, home.center_z - 2), BLOCK_GOLD_ORE);
    // Shell: walls at +-5 are solid cobble/stone (never air, never light).
    EXPECT_TRUE(home.at(home.center_x + 5, home.floor_y, home.center_z) == BLOCK_COBBLESTONE ||
                home.at(home.center_x + 5, home.floor_y, home.center_z) == BLOCK_STONE);
    EXPECT_TRUE(home.at(home.center_x, home.floor_y - 1, home.center_z + 5) == BLOCK_COBBLESTONE ||
                home.at(home.center_x, home.floor_y - 1, home.center_z + 5) == BLOCK_STONE);
    EXPECT_NE(home.at(home.center_x + 5, home.floor_y + 2, home.center_z), BLOCK_AIR);
    EXPECT_NE(home.at(home.center_x, home.floor_y + 4, home.center_z), BLOCK_AIR); // ceiling
    // Fully dark: no glowstone/torch anywhere in the footprint.
    for (int y = home.floor_y - 1; y <= home.floor_y + 4; ++y)
        for (int dz = -5; dz <= 5; ++dz)
            for (int dx = -5; dx <= 5; ++dx) {
                BlockId b = home.at(home.center_x + dx, y, home.center_z + dz);
                EXPECT_TRUE(b != BLOCK_GLOWSTONE && b != BLOCK_TORCH);
            }

    // A neighboring chunk in the same region must NOT contain the room.
    DungeonFixture neighbor(777, dcx + 1, dcz, dfy, /*place=*/true);
    EXPECT_NE(neighbor.at(neighbor.center_x, home.floor_y + 1, neighbor.center_z),
              BLOCK_DIAMOND_ORE);
}

TEST(StructureGenerator, DungeonSpawnsInGeneratedWorld) {
    // End-to-end through WorldGenerator: some dungeon region within radius 2
    // of the origin must produce the room in its home chunk.
    StructureGenerator gen(4242);
    bool found = false;
    for (int rx = -1; rx <= 1 && !found; ++rx) {
        for (int rz = -1; rz <= 1 && !found; ++rz) {
            int dcx, dcz, dfy;
            ASSERT_TRUE(gen.get_dungeon_in_region(rx, rz, dcx, dcz, dfy));
            DungeonFixture f(4242, dcx, dcz, dfy, /*place=*/true);
            EXPECT_EQ(f.at(f.center_x, dfy + 1, f.center_z), BLOCK_DIAMOND_ORE);
            EXPECT_EQ(f.at(f.center_x, dfy + 2, f.center_z), BLOCK_AIR);
            found = true; // every region hosts exactly one dungeon
        }
    }
    EXPECT_TRUE(found) << "each region must generate its dungeon";
}

// ---- Underground spawn rule (dungeons populate at any hour) ----

TEST(StructureGenerator, DungeonSpawnsMonstersUndergroundAtNoon) {
    StructureGenerator gen(4242);
    int dcx, dcz, dfy;
    ASSERT_TRUE(gen.get_dungeon_in_region(0, 0, dcx, dcz, dfy));

    World world(4242, DimensionId::Overworld);
    auto chunk = World::chunk_pool.acquire();
    chunk->reset({dcx, dcz});
    WorldGenerator wgen(4242);
    wgen.generate(*chunk);
    world.insert_chunk(std::move(chunk));

    MobSpawner spawner;
    // Player far away (>24 blocks): the proximity rule must not block spawns.
    std::vector<Vec3> far_player{Vec3(static_cast<float>(dcx * CHUNK_SIZE + 8),
                                      90.0f,
                                      static_cast<float>(dcz * CHUNK_SIZE + 8 + 40))};

    const float NOON = 0.5f, MIDNIGHT = 0.0f;
    // Floor tile next to the central pedestal (pedestal occupies the center).
    BlockPos interior(dcx * CHUNK_SIZE + 8 + 1, dfy, dcz * CHUNK_SIZE + 8);
    BlockPos wall(dcx * CHUNK_SIZE + 8 + 5, dfy, dcz * CHUNK_SIZE + 8);

    // Dark buried room: spawns at noon AND midnight.
    EXPECT_TRUE(spawner.can_spawn_at(world, interior, MobCategory::Monster, far_player, NOON));
    EXPECT_TRUE(spawner.can_spawn_at(world, interior, MobCategory::Monster, far_player, MIDNIGHT));

    // Solid shell wall: never a spawn position.
    EXPECT_FALSE(spawner.can_spawn_at(world, wall, MobCategory::Monster, far_player, NOON));
}

} // namespace mc

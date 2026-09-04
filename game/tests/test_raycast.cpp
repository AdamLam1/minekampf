#include <gtest/gtest.h>

#include "core/config.hpp"
#include "physics/raycast.hpp"
#include "world/block.hpp"
#include "world/chunk.hpp"
#include "world/world.hpp"

TEST(Raycast, HitsSolidBlock) {
    mc::World w(1ULL);
    auto& chunk = w.create_chunk({0, 0});
    (void) chunk.set_block(5, 10, 0, mc::BLOCK_STONE);

    auto hit = voxel_raycast(w, mc::Vec3(0.5f, 10.5f, 0.5f), mc::Vec3(1.0f, 0.0f, 0.0f), 20.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->block_pos, (mc::BlockPos{5, 10, 0}));
    // Ray entered from the west (-X) face of the block -> face = West.
    EXPECT_EQ(hit->face, mc::Direction::West);
}

TEST(Raycast, MissesAir) {
    mc::World w(1ULL);
    (void) w.create_chunk({0, 0}); // all air
    auto hit = voxel_raycast(w, mc::Vec3(0.5f, 10.5f, 0.5f), mc::Vec3(1.0f, 0.0f, 0.0f), 5.0f);
    EXPECT_FALSE(hit.has_value());
}

TEST(Raycast, RespectsMaxDistance) {
    mc::World w(1ULL);
    auto& chunk = w.create_chunk({0, 0});
    (void) chunk.set_block(50, 10, 0, mc::BLOCK_STONE);
    auto hit = voxel_raycast(w, mc::Vec3(0.5f, 10.5f, 0.5f), mc::Vec3(1.0f, 0.0f, 0.0f), 10.0f);
    EXPECT_FALSE(hit.has_value());
}

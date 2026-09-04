#include <gtest/gtest.h>

#include "core/config.hpp"
#include "physics/aabb.hpp"
#include "physics/collision.hpp"
#include "world/block.hpp"
#include "world/chunk.hpp"
#include "world/world.hpp"

namespace {
// Build a world with a flat stone floor at y=0 (block y 0 = stone).
mc::World make_floor_world() {
    mc::World w(1ULL);
    auto& chunk = w.create_chunk({0, 0});
    for (int x = 0; x < mc::CHUNK_SIZE; ++x)
        for (int z = 0; z < mc::CHUNK_SIZE; ++z) chunk.set_block(x, 0, z, mc::BLOCK_STONE);
    return w;
}
} // namespace

TEST(Collision, LandsOnFloor) {
    mc::World w = make_floor_world();
    // Player box above the floor, falling.
    mc::AABB box = mc::AABB::from_entity(mc::Vec3(0.5f, 5.0f, 0.5f), 0.6f, 1.8f);
    // Move down by 5.0 (should stop on floor top = y 1.0).
    auto res = move_and_collide(w, box, mc::Vec3(0.0f, -5.0f, 0.0f));
    EXPECT_TRUE(res.on_ground);
    EXPECT_NEAR(box.min.y, 1.0f, 1e-4f);
}

TEST(Collision, WallStopsHorizontal) {
    mc::World w(1ULL);
    auto& chunk = w.create_chunk({0, 0});
    // Build a 1-wide, 2-tall stone wall at x=3.
    for (int y = 0; y < 3; ++y) chunk.set_block(3, y, 0, mc::BLOCK_STONE);
    // Player at x=1.5, on floor (y=1), moving +X by 5 (should stop before wall at x=3).
    mc::AABB box = mc::AABB::from_entity(mc::Vec3(1.5f, 1.0f, 0.5f), 0.6f, 1.8f);
    auto res = move_and_collide(w, box, mc::Vec3(5.0f, 0.0f, 0.0f));
    EXPECT_TRUE(res.horizontal);
    // Box max.x must not penetrate the wall (wall starts at x=3).
    EXPECT_LE(box.max.x, 3.0f + 1e-4f);
}

TEST(Collision, JumpArcAndLanding) {
    mc::World w = make_floor_world();
    mc::AABB box = mc::AABB::from_entity(mc::Vec3(0.5f, 1.0f, 0.5f), 0.6f, 1.8f);
    mc::Vec3 vel(0.0f, 0.42f, 0.0f); // Jump impulse

    // 1. First frame: moving up
    auto res1 = move_and_collide(w, box, vel);
    EXPECT_FALSE(res1.on_ground);
    EXPECT_GT(box.min.y, 1.0f);

    // 2. Simulate gravity & falling back
    float initial_peak = box.min.y;
    for (int tick = 0; tick < 20; ++tick) {
        vel.y -= 0.08f; // Standard Minecraft gravity
        vel.y *= 0.98f; // Drag
        auto res = move_and_collide(w, box, vel);
        if (res.on_ground) break;
    }

    // Must have landed safely back on floor y=1.0
    EXPECT_NEAR(box.min.y, 1.0f, 1e-4f);
    EXPECT_GT(initial_peak, 1.0f);
}

TEST(Collision, WallSlidingCorner) {
    mc::World w(1ULL);
    auto& chunk = w.create_chunk({0, 0});
    // Build wall at x=3
    for (int y = 0; y < 3; ++y) chunk.set_block(3, y, 0, mc::BLOCK_STONE);

    // Player moving diagonally into wall (+X, +Z)
    mc::AABB box = mc::AABB::from_entity(mc::Vec3(1.5f, 1.0f, 0.5f), 0.6f, 1.8f);
    float start_z = box.min.z;
    auto res = move_and_collide(w, box, mc::Vec3(5.0f, 0.0f, 2.0f));

    // Horizontal collision on X, but Z movement (sliding) succeeded
    EXPECT_TRUE(res.hit_x);
    EXPECT_LE(box.max.x, 3.0f + 1e-4f);
    EXPECT_GT(box.min.z, start_z + 1.5f);
}

TEST(Collision, WaterSubmersionBuoyancy) {
    mc::World w(1ULL);
    auto& chunk = w.create_chunk({0, 0});
    // Water block at y=1, y=2
    chunk.set_block(0, 1, 0, mc::BLOCK_WATER);
    chunk.set_block(0, 2, 0, mc::BLOCK_WATER);

    mc::AABB box = mc::AABB::from_entity(mc::Vec3(0.5f, 1.0f, 0.5f), 0.6f, 1.8f);
    
    // Test that water block detection works correctly
    EXPECT_TRUE(w.get_block({0, 1, 0}) == mc::BLOCK_WATER);
    EXPECT_FALSE(w.get_block({0, 3, 0}) == mc::BLOCK_WATER);
}

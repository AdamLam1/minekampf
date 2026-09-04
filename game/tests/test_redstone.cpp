#include <gtest/gtest.h>

#include "gameplay/redstone.hpp"
#include "world/chunk.hpp"
#include "world/world.hpp"

namespace mc {

namespace {
// Builds a lever -- wire -- lamp row on flat ground and ticks the system.
struct Rig {
    World world{1ULL};
    RedstoneSystem rs;
    BlockPos lever{2, 10, 0};
    BlockPos wire{3, 10, 0};
    BlockPos lamp{4, 10, 0};

    Rig() {
        world.create_chunk({0, 0});
        world.set_block(lever, BLOCK_LEVER_OFF);
        world.set_block(wire, BLOCK_REDSTONE_WIRE);
        world.set_block(lamp, BLOCK_REDSTONE_LAMP_OFF);
        rs.on_block_changed(world, lever);
        rs.on_block_changed(world, wire);
        rs.on_block_changed(world, lamp);
    }
    void tick() { rs.tick(world, {}); }
};
} // namespace

TEST(RedstoneComponents, LeverOffLeavesWireAndLampOff) {
    Rig rig;
    rig.tick();
    EXPECT_EQ(rig.world.get_block(rig.wire), BLOCK_REDSTONE_WIRE);
    EXPECT_EQ(rig.world.get_block(rig.lamp), BLOCK_REDSTONE_LAMP_OFF);
}

TEST(RedstoneComponents, LeverOnPowersWireAndLamp) {
    Rig rig;
    rig.world.set_block(rig.lever, BLOCK_LEVER_ON);
    rig.rs.on_block_changed(rig.world, rig.lever);
    rig.tick();
    EXPECT_GT(rig.rs.get_power(rig.wire), 0);
    EXPECT_EQ(rig.world.get_block(rig.wire), BLOCK_REDSTONE_WIRE_POWER_14); // 15-1 from source
    EXPECT_EQ(rig.world.get_block(rig.lamp), BLOCK_REDSTONE_LAMP_ON);
}

TEST(RedstoneComponents, LeverOffAgainUnpowersLamp) {
    Rig rig;
    rig.world.set_block(rig.lever, BLOCK_LEVER_ON);
    rig.rs.on_block_changed(rig.world, rig.lever);
    rig.tick();
    EXPECT_EQ(rig.world.get_block(rig.lamp), BLOCK_REDSTONE_LAMP_ON);

    rig.world.set_block(rig.lever, BLOCK_LEVER_OFF);
    rig.rs.on_block_changed(rig.world, rig.lever);
    rig.tick();
    EXPECT_EQ(rig.world.get_block(rig.lamp), BLOCK_REDSTONE_LAMP_OFF);
}

TEST(RedstoneComponents, BreakingLampUnregistersIt) {
    Rig rig;
    rig.world.set_block(rig.lever, BLOCK_LEVER_ON);
    rig.rs.on_block_changed(rig.world, rig.lever);
    rig.tick();
    ASSERT_EQ(rig.world.get_block(rig.lamp), BLOCK_REDSTONE_LAMP_ON);

    rig.world.set_block(rig.lamp, BLOCK_AIR);
    rig.rs.on_block_changed(rig.world, rig.lamp);
    rig.tick();
    EXPECT_EQ(rig.world.get_block(rig.lamp), BLOCK_AIR);
}

TEST(RedstoneComponents, RepeaterDelaysSignalByTwoTicks) {
    World world{1ULL};
    RedstoneSystem rs;
    world.create_chunk({0, 0});
    BlockPos lever{2, 10, 0};
    BlockPos rep{3, 10, 0};
    world.set_block(lever, BLOCK_LEVER_OFF);
    world.set_block(rep, BLOCK_REPEATER_OFF);
    rs.on_block_changed(world, lever);
    rs.on_block_changed(world, rep);

    // Turn the lever on: the repeater must lag behind by 2 ticks.
    world.set_block(lever, BLOCK_LEVER_ON);
    rs.on_block_changed(world, lever);
    rs.tick(world, {});
    rs.tick(world, {});
    EXPECT_EQ(world.get_block(rep), BLOCK_REPEATER_OFF);
    rs.tick(world, {});
    EXPECT_EQ(world.get_block(rep), BLOCK_REPEATER_ON);
}

TEST(RedstoneComponents, RepeaterCarriesPowerToLamp) {
    World world{1ULL};
    RedstoneSystem rs;
    world.create_chunk({0, 0});
    BlockPos lever{2, 10, 0};
    BlockPos rep{3, 10, 0};
    BlockPos lamp{4, 10, 0};
    world.set_block(lever, BLOCK_LEVER_OFF);
    world.set_block(rep, BLOCK_REPEATER_OFF);
    world.set_block(lamp, BLOCK_REDSTONE_LAMP_OFF);
    rs.on_block_changed(world, lever);
    rs.on_block_changed(world, rep);
    rs.on_block_changed(world, lamp);

    world.set_block(lever, BLOCK_LEVER_ON);
    rs.on_block_changed(world, lever);
    for (int i = 0; i < 5; ++i) rs.tick(world, {});
    EXPECT_EQ(world.get_block(lamp), BLOCK_REDSTONE_LAMP_ON);
}

TEST(RedstoneComponents, PressurePlatePressesAndReleases) {
    World world{1ULL};
    RedstoneSystem rs;
    world.create_chunk({0, 0});
    BlockPos plate{2, 10, 0};
    BlockPos lamp{3, 10, 0};
    world.set_block(plate, BLOCK_PRESSURE_PLATE_OFF);
    world.set_block(lamp, BLOCK_REDSTONE_LAMP_OFF);
    rs.on_block_changed(world, plate);
    rs.on_block_changed(world, lamp);

    Vec3 entity{2.5f, 10.0f, 0.5f};
    rs.tick(world, {entity});
    rs.tick(world, {entity});
    EXPECT_EQ(world.get_block(plate), BLOCK_PRESSURE_PLATE_ON);
    EXPECT_EQ(world.get_block(lamp), BLOCK_REDSTONE_LAMP_ON);

    // Entity leaves: plate releases and the lamp follows.
    rs.tick(world, {Vec3{20.5f, 10.0f, 0.5f}});
    rs.tick(world, {});
    EXPECT_EQ(world.get_block(plate), BLOCK_PRESSURE_PLATE_OFF);
    EXPECT_EQ(world.get_block(lamp), BLOCK_REDSTONE_LAMP_OFF);
}

TEST(RedstoneComponents, PlateIgnoresDistantEntity) {
    World world{1ULL};
    RedstoneSystem rs;
    world.create_chunk({0, 0});
    BlockPos plate{2, 10, 0};
    world.set_block(plate, BLOCK_PRESSURE_PLATE_OFF);
    rs.on_block_changed(world, plate);

    rs.tick(world, {Vec3{2.5f, 11.5f, 0.5f}}); // one block above: not standing
    EXPECT_EQ(world.get_block(plate), BLOCK_PRESSURE_PLATE_OFF);
}

} // namespace mc

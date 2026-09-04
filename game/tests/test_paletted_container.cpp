#include <gtest/gtest.h>

#include "world/block.hpp"
#include "world/paletted_container.hpp"

using mc::PalettedContainer;

TEST(PalettedContainer, DefaultIsAir) {
    PalettedContainer c;
    EXPECT_EQ(c.get(0, 0, 0), mc::BLOCK_AIR);
    EXPECT_EQ(c.get(15, 15, 15), mc::BLOCK_AIR);
}

TEST(PalettedContainer, SetGetRoundtrip) {
    PalettedContainer c;
    c.set(1, 2, 3, mc::BLOCK_STONE);
    EXPECT_EQ(c.get(1, 2, 3), mc::BLOCK_STONE);
    EXPECT_EQ(c.get(0, 0, 0), mc::BLOCK_AIR);
    c.set(15, 15, 15, mc::BLOCK_DIRT);
    EXPECT_EQ(c.get(15, 15, 15), mc::BLOCK_DIRT);
}

TEST(PalettedContainer, Fill) {
    PalettedContainer c;
    c.fill(mc::BLOCK_STONE);
    for (int i = 0; i < mc::SECTION_VOLUME; ++i) EXPECT_EQ(c.get_linear(i), mc::BLOCK_STONE);
}

TEST(PalettedContainer, PaletteGrowth) {
    // Setting many distinct blocks forces palette resizes (4 -> 5 -> ... bits).
    PalettedContainer c;
    mc::BlockId ids[] = {mc::BLOCK_STONE, mc::BLOCK_DIRT, mc::BLOCK_GRASS, mc::BLOCK_SAND,
                         mc::BLOCK_GRAVEL, mc::BLOCK_GRANITE, mc::BLOCK_DIORITE, mc::BLOCK_ANDESITE,
                         mc::BLOCK_COAL_ORE, mc::BLOCK_IRON_ORE, mc::BLOCK_GOLD_ORE, mc::BLOCK_DIAMOND_ORE,
                         mc::BLOCK_COBBLESTONE, mc::BLOCK_OAK_PLANKS, mc::BLOCK_OAK_LOG, mc::BLOCK_OAK_LEAVES,
                         mc::BLOCK_GLASS, mc::BLOCK_BRICKS, mc::BLOCK_SANDSTONE, mc::BLOCK_BEDROCK};
    for (int i = 0; i < 20; ++i) c.set(i % 16, (i / 16) % 16, (i / 256) % 16, ids[i]);
    for (int i = 0; i < 20; ++i) EXPECT_EQ(c.get(i % 16, (i / 16) % 16, (i / 256) % 16), ids[i]);
}

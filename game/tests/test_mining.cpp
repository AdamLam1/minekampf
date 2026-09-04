#include <gtest/gtest.h>
#include "gameplay/mining.hpp"

namespace mc {

TEST(MiningTest, BedrockIsUnbreakable) {
    EXPECT_TRUE(mining::unbreakable(BLOCK_BEDROCK));
    EXPECT_EQ(mining::break_time_seconds(BLOCK_BEDROCK, ITEM_DIAMOND_PICKAXE), mining::UNBREAKABLE);
}

TEST(MiningTest, FoliageBreaksInstantly) {
    EXPECT_EQ(mining::break_time_seconds(BLOCK_TALL_GRASS, ITEM_AIR), 0.0f);
}

TEST(MiningTest, PickaxesSpeedUpStone) {
    float hand = mining::break_time_seconds(BLOCK_STONE, ITEM_AIR);
    float wood = mining::break_time_seconds(BLOCK_STONE, ITEM_WOODEN_PICKAXE);
    float iron = mining::break_time_seconds(BLOCK_STONE, ITEM_IRON_PICKAXE);
    EXPECT_GT(hand, wood);
    EXPECT_GT(wood, iron);
    EXPECT_FLOAT_EQ(wood, mining::hardness(BLOCK_STONE) / 2.0f);
}

TEST(MiningTest, WrongToolClassGetsPenalty) {
    // Shovels don't help on stone: slower than bare hand hard-rock factor.
    float wrong = mining::break_time_seconds(BLOCK_STONE, ITEM_AIR);
    float dirt_with_wrong_tool = mining::break_time_seconds(BLOCK_DIRT, ITEM_STONE_PICKAXE);
    float dirt_hand = mining::break_time_seconds(BLOCK_DIRT, ITEM_AIR);
    EXPECT_FLOAT_EQ(dirt_with_wrong_tool, dirt_hand * 3.33f);
    EXPECT_GT(dirt_hand, 0.0f);
    EXPECT_GT(wrong, 0.0f);
}

// ---- Drop gating ----

TEST(MiningTest, StoneWithoutPickaxeYieldsNothing) {
    auto d = mining::drop_for(BLOCK_STONE, ITEM_AIR);
    EXPECT_FALSE(d.has_value());
}

TEST(MiningTest, StoneWithWoodenPickaxeYieldsCobblestone) {
    auto d = mining::drop_for(BLOCK_STONE, ITEM_WOODEN_PICKAXE);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->item, ITEM_COBBLESTONE);
    EXPECT_EQ(d->count_min, 1);
}

TEST(MiningTest, IronOreRequiresStonePickaxeTier) {
    EXPECT_FALSE(mining::drop_for(BLOCK_IRON_ORE, ITEM_WOODEN_PICKAXE).has_value());
    auto d = mining::drop_for(BLOCK_IRON_ORE, ITEM_STONE_PICKAXE);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(static_cast<BlockId>(d->item), BLOCK_IRON_ORE); // ore item until smelting exists
}

TEST(MiningTest, DiamondOreRequiresIronPickaxeTier) {
    EXPECT_FALSE(mining::drop_for(BLOCK_DIAMOND_ORE, ITEM_WOODEN_PICKAXE).has_value());
    EXPECT_FALSE(mining::drop_for(BLOCK_DIAMOND_ORE, ITEM_STONE_PICKAXE).has_value());
    auto d = mining::drop_for(BLOCK_DIAMOND_ORE, ITEM_IRON_PICKAXE);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->item, ITEM_DIAMOND);
}

TEST(MiningTest, HandBlocksDropThemselvesOrAlternative) {
    auto dirt = mining::drop_for(BLOCK_DIRT, ITEM_AIR);
    ASSERT_TRUE(dirt.has_value());
    EXPECT_EQ(dirt->item, ITEM_DIRT);

    auto grass = mining::drop_for(BLOCK_GRASS, ITEM_AIR);
    ASSERT_TRUE(grass.has_value());
    EXPECT_EQ(grass->item, ITEM_DIRT); // vanilla: grass breaks into dirt

    auto sand = mining::drop_for(BLOCK_SAND, ITEM_AIR);
    ASSERT_TRUE(sand.has_value());
    EXPECT_EQ(sand->item, static_cast<ItemId>(BLOCK_SAND));
}

TEST(MiningTest, GlassAndDecorBreakIntoNothing) {
    EXPECT_FALSE(mining::drop_for(BLOCK_GLASS, ITEM_AIR).has_value());
    // Tall grass DOES drop (plant fiber for bows); flowers still shatter.
    EXPECT_FALSE(mining::drop_for(BLOCK_YELLOW_FLOWER, ITEM_AIR).has_value());
    EXPECT_FALSE(mining::drop_for(BLOCK_RED_FLOWER, ITEM_AIR).has_value());
}

// ---- Chance resolution ----

TEST(MiningTest, AppleChanceResolution) {
    auto leaves = mining::drop_for(BLOCK_OAK_LEAVES, ITEM_AIR);
    ASSERT_TRUE(leaves.has_value());

    // Below 15% => apple; above => nothing.
    ItemStack lucky = mining::resolve_drop(*leaves, 0.05f);
    EXPECT_FALSE(lucky.is_empty());
    EXPECT_EQ(lucky.item, ITEM_APPLE);

    ItemStack unlucky = mining::resolve_drop(*leaves, 0.50f);
    EXPECT_TRUE(unlucky.is_empty());
}

TEST(MiningTest, FixedCountIgnoresChance) {
    mining::Drop fixed{ITEM_DIRT, 1, 1};
    ItemStack always = mining::resolve_drop(fixed, 0.999f);
    EXPECT_EQ(always.item, ITEM_DIRT);
    EXPECT_EQ(always.count, 1);
}

// ---- Weapons & food ----

TEST(MiningTest, SwordsOutdamageFistMonotonically) {
    float prev = mining::weapon_damage(ITEM_AIR);
    const ItemId swords[] = {ITEM_WOODEN_SWORD, ITEM_STONE_SWORD, ITEM_IRON_SWORD, ITEM_DIAMOND_SWORD};
    for (ItemId s : swords) {
        EXPECT_GT(mining::weapon_damage(s), prev);
        prev = mining::weapon_damage(s);
    }
}

TEST(MiningTest, FoodRegistry) {
    auto apple = mining::food_value(ITEM_APPLE);
    ASSERT_TRUE(apple.has_value());
    EXPECT_EQ(apple->nutrition, 4);

    auto meat = mining::food_value(ITEM_RAW_MEAT);
    ASSERT_TRUE(meat.has_value());
    EXPECT_EQ(meat->nutrition, 3);

    EXPECT_FALSE(mining::food_value(ITEM_STICK).has_value());
}

} // namespace mc

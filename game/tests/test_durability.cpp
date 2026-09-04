#include <gtest/gtest.h>
#include "gameplay/mining.hpp"

namespace mc {

// ---- Tool durability (ItemStack.damage vs max_damage 59/131/250/1561) ----

TEST(DurabilityTest, ToolBreaksExactlyAtMaxDamage) {
    ItemStack pick(ITEM_WOODEN_PICKAXE); // max_damage 59
    for (int i = 0; i < 58; ++i) {
        EXPECT_FALSE(mining::damage_tool(pick, 1)) << "broke early at use " << i + 1;
        ASSERT_FALSE(pick.is_empty());
    }
    EXPECT_EQ(pick.damage, 58);

    EXPECT_TRUE(mining::damage_tool(pick, 1)) << "59th use must snap the tool";
    EXPECT_TRUE(pick.is_empty()) << "broken tool leaves the slot";
}

TEST(DurabilityTest, OvershootWearStillConsumes) {
    ItemStack sword(ITEM_DIAMOND_SWORD); // max_damage 1561
    sword.damage = 1560;
    EXPECT_TRUE(mining::damage_tool(sword, 5)); // overshoot clamps to break
    EXPECT_TRUE(sword.is_empty());
}

TEST(DurabilityTest, PlainItemsNeverWear) {
    ItemStack dirt(ITEM_DIRT, 10);
    EXPECT_FALSE(mining::damage_tool(dirt, 1));
    EXPECT_EQ(dirt.count, 10);
    EXPECT_EQ(dirt.damage, 0);

    ItemStack empty;
    EXPECT_FALSE(mining::damage_tool(empty, 1));
}

TEST(DurabilityTest, AlreadyWornOutStackIsInert) {
    // The normal flow consumes a tool at the crossing use, so a pre-worn-out
    // stack can only appear via save tampering; wear must be a no-op on it.
    ItemStack pick(ITEM_STONE_PICKAXE); // 131
    pick.damage = 131;
    EXPECT_FALSE(mining::damage_tool(pick, 1));
    EXPECT_FALSE(pick.is_empty());
    EXPECT_EQ(pick.damage, 131);
}

TEST(DurabilityTest, AttackCostWeaponsVsTools) {
    EXPECT_EQ(mining::attack_durability_cost(ITEM_WOODEN_SWORD), 1);
    EXPECT_EQ(mining::attack_durability_cost(ITEM_DIAMOND_SWORD), 1);
    EXPECT_EQ(mining::attack_durability_cost(ITEM_STONE_PICKAXE), 2);
    EXPECT_EQ(mining::attack_durability_cost(ITEM_DIAMOND_PICKAXE), 2);
    EXPECT_EQ(mining::attack_durability_cost(ITEM_AIR), 0); // fists are free
    EXPECT_EQ(mining::attack_durability_cost(ITEM_STICK), 0);
}

TEST(DurabilityTest, WearOnlyAppliesToResistingBlocks) {
    EXPECT_TRUE(mining::wear_applies(BLOCK_STONE));
    EXPECT_TRUE(mining::wear_applies(BLOCK_DIRT));
    EXPECT_FALSE(mining::wear_applies(BLOCK_TALL_GRASS)); // hardness 0
    EXPECT_FALSE(mining::wear_applies(BLOCK_BEDROCK));    // unbreakable
    EXPECT_FALSE(mining::wear_applies(BLOCK_AIR));
}

TEST(DurabilityTest, TierLimitsMatchVanillaExpectations) {
    // Sanity lock so a future table refactor cannot silently open gates.
    EXPECT_EQ(mining::required_tier(BLOCK_STONE), mining::MaterialTier::Wood);
    EXPECT_EQ(mining::required_tier(BLOCK_IRON_ORE), mining::MaterialTier::Stone);
    EXPECT_EQ(mining::required_tier(BLOCK_DIAMOND_ORE), mining::MaterialTier::Iron);
    EXPECT_EQ(mining::required_tier(BLOCK_DIRT), mining::MaterialTier::Hand);
}

} // namespace mc

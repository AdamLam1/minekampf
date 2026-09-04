#include <gtest/gtest.h>
#include "gameplay/enchanting.hpp"
#include "gameplay/mining.hpp"
#include "world/block.hpp"

namespace mc {

TEST(EnchantingSystem, PriorWorkPenalty) {
    EXPECT_EQ(EnchantingSystem::calculate_prior_work_penalty(0), 0);
    EXPECT_EQ(EnchantingSystem::calculate_prior_work_penalty(1), 1);
    EXPECT_EQ(EnchantingSystem::calculate_prior_work_penalty(2), 3);
    EXPECT_EQ(EnchantingSystem::calculate_prior_work_penalty(3), 7);
    EXPECT_EQ(EnchantingSystem::calculate_prior_work_penalty(4), 15);
    EXPECT_EQ(EnchantingSystem::calculate_prior_work_penalty(5), 31);
}

TEST(EnchantingSystem, AnvilCost) {
    // base_cost = 5, prior_work_penalty = 3 (2 uses), rename_cost = 1
    // Total = 9
    EXPECT_EQ(EnchantingSystem::calculate_anvil_cost(5, 3, 1), 9);
    
    // Too expensive (e.g. 5 uses = penalty 31 + base cost 10) = 41 -> -1
    EXPECT_EQ(EnchantingSystem::calculate_anvil_cost(10, 31, 0), -1);
}

TEST(EnchantingSystem, SlotLevels) {
    // 0 bookshelves
    EXPECT_GE(EnchantingSystem::calculate_enchantment_slot_level(1, 0), 1);
    EXPECT_GE(EnchantingSystem::calculate_enchantment_slot_level(3, 0), 3);

    // 15 bookshelves -> max levels
    // Should be capped at 30 for slot 3
    EXPECT_EQ(EnchantingSystem::calculate_enchantment_slot_level(3, 15), 30);
}

TEST(EnchantLevelPacking, RoundTrip) {
    uint16_t packed = 0;
    EXPECT_EQ(enchant_level(packed, EnchantType::Sharpness), 0);

    packed = with_enchant_level(packed, EnchantType::Sharpness, 3);
    packed = with_enchant_level(packed, EnchantType::Efficiency, 5);
    EXPECT_EQ(enchant_level(packed, EnchantType::Sharpness), 3);
    EXPECT_EQ(enchant_level(packed, EnchantType::Efficiency), 5);
    EXPECT_EQ(enchant_level(packed, EnchantType::Protection), 0);

    // Levels clamp to 5; setting 0 clears the nibble.
    packed = with_enchant_level(packed, EnchantType::Unbreaking, 9);
    EXPECT_EQ(enchant_level(packed, EnchantType::Unbreaking), ENCHANT_MAX_LEVEL);
    packed = with_enchant_level(packed, EnchantType::Sharpness, 0);
    EXPECT_EQ(enchant_level(packed, EnchantType::Sharpness), 0);
}

TEST(ItemStackEnchants, StackCompatAndAccessors) {
    ItemStack plain(ITEM_IRON_SWORD, 1);
    ItemStack shiny(ITEM_IRON_SWORD, 1);
    shiny.enchant_levels = with_enchant_level(0, EnchantType::Sharpness, 2);

    EXPECT_FALSE(plain.can_stack_with(shiny));
    EXPECT_FALSE(shiny.can_stack_with(plain));
    EXPECT_TRUE(shiny.is_enchanted());
    EXPECT_FALSE(plain.is_enchanted());
    EXPECT_EQ(shiny.enchant_level_of(EnchantType::Sharpness), 2);
}

TEST(EnchantingSystem, EnchantableItemKinds) {
    EXPECT_EQ(EnchantingSystem::enchant_for_item(ITEM_DIAMOND_SWORD), EnchantType::Sharpness);
    EXPECT_EQ(EnchantingSystem::enchant_for_item(ITEM_IRON_PICKAXE), EnchantType::Efficiency);
    EXPECT_EQ(EnchantingSystem::enchant_for_item(ITEM_BOW), EnchantType::Unbreaking);
    EXPECT_TRUE(EnchantingSystem::is_enchantable(ITEM_WOODEN_SWORD));
    EXPECT_FALSE(EnchantingSystem::is_enchantable(ITEM_APPLE));
    EXPECT_FALSE(EnchantingSystem::is_enchantable(BLOCK_STONE));
}

TEST(EnchantingSystem, RollOffersDeterministic) {
    EnchantOffer a[3];
    EnchantOffer b[3];
    EnchantingSystem::roll_offers(42u, 0xDEADBEEF, 15, a);
    EnchantingSystem::roll_offers(42u, 0xDEADBEEF, 15, b);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(a[i].xp_cost, b[i].xp_cost);
        EXPECT_EQ(a[i].level, b[i].level);
        EXPECT_GE(a[i].level, 1);
        EXPECT_LE(a[i].level, ENCHANT_MAX_LEVEL);
        EXPECT_GE(a[i].xp_cost, 1);
    }
    // More bookshelves must not lower the slot-3 power.
    EnchantOffer no_shelves[3];
    EnchantingSystem::roll_offers(42u, 0xDEADBEEF, 0, no_shelves);
    EXPECT_GE(a[2].level, no_shelves[2].level);
}

TEST(EnchantingSystem, ApplyAndUpgrade) {
    ItemStack sword(ITEM_IRON_SWORD, 1);

    EnchantOffer offer{5, EnchantType::Sharpness, 2};
    ASSERT_TRUE(EnchantingSystem::can_apply(offer, sword, 10));
    ItemStack enchanted = EnchantingSystem::apply(offer, sword);
    EXPECT_EQ(enchanted.enchant_level_of(EnchantType::Sharpness), 2);

    // Same level again is not an upgrade -> cannot re-apply.
    EXPECT_FALSE(EnchantingSystem::can_apply(offer, enchanted, 10));
    // Higher level is.
    EnchantOffer better{10, EnchantType::Sharpness, 4};
    EXPECT_TRUE(EnchantingSystem::can_apply(better, enchanted, 10));
    // Not enough levels.
    EXPECT_FALSE(EnchantingSystem::can_apply(better, enchanted, 3));
    // Wrong item type.
    EXPECT_FALSE(EnchantingSystem::can_apply(better, ItemStack(ITEM_APPLE, 1), 30));
}

TEST(MiningEnchantEffects, EfficiencySpeedsMining) {
    float base = mining::break_time_seconds(BLOCK_STONE, ITEM_IRON_PICKAXE);
    ItemStack pick(ITEM_IRON_PICKAXE, 1);
    EXPECT_FLOAT_EQ(mining::break_time_seconds(BLOCK_STONE, pick), base);

    pick.enchant_levels = with_enchant_level(0, EnchantType::Efficiency, 3);
    EXPECT_LT(mining::break_time_seconds(BLOCK_STONE, pick), base);
}

TEST(MiningEnchantEffects, UnbreakingProbability) {
    EXPECT_FALSE(mining::unbreaking_blocks(0, 0.0f));  // level 0 never blocks
    EXPECT_TRUE(mining::unbreaking_blocks(3, 0.0f));   // low roll always blocks
    EXPECT_FALSE(mining::unbreaking_blocks(3, 0.9f));  // high roll never blocks
}

} // namespace mc

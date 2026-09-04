#include <gtest/gtest.h>
#include "gameplay/mining.hpp"

namespace mc {

// ---- Ballistic curves ----

TEST(BowTest, ChargeIsCappedAtOneSecond) {
    EXPECT_FLOAT_EQ(mining::bow_charge_capped(0.5f), 0.5f);
    EXPECT_FLOAT_EQ(mining::bow_charge_capped(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(mining::bow_charge_capped(5.0f), 1.0f);
}

TEST(BowTest, SpeedGrowsWithDrawAndCaps) {
    EXPECT_FLOAT_EQ(mining::bow_speed(0.0f), 0.5f);   // fizzle-speed floor
    EXPECT_FLOAT_EQ(mining::bow_speed(0.5f), 1.25f);
    EXPECT_FLOAT_EQ(mining::bow_speed(1.0f), 2.0f);
    EXPECT_FLOAT_EQ(mining::bow_speed(10.0f), 2.0f);  // no overdraw bonus
}

TEST(BowTest, DamageGrowsWithDrawAndCaps) {
    EXPECT_FLOAT_EQ(mining::bow_damage(0.0f), 1.0f);  // fist baseline
    EXPECT_FLOAT_EQ(mining::bow_damage(0.25f), 3.0f);
    EXPECT_FLOAT_EQ(mining::bow_damage(1.0f), 9.0f);
    EXPECT_FLOAT_EQ(mining::bow_damage(3.3f), 9.0f);  // no overdraw bonus
}

TEST(BowTest, MinimumChargeGate) {
    // A quick tap (below the gate) must not fire: the gate is well below the
    // damage floor curve so any legal shot does more damage than a punch.
    float tap = 0.1f;
    EXPECT_LT(tap, mining::BOW_MIN_CHARGE);
    EXPECT_GT(mining::bow_damage(mining::BOW_MIN_CHARGE), 1.0f);
}

TEST(BowTest, GravityDropEstimator) {
    // Flight time to 10 blocks at full draw (2.0/tick) = 5 ticks.
    // Drop = 0.5 * 0.045 * 25 = 0.5625 blocks — aim must compensate.
    EXPECT_NEAR(mining::arrow_drop(10.0f, mining::bow_speed(1.0f)), 0.5625f, 0.001f);
    EXPECT_FLOAT_EQ(mining::arrow_drop(0.0f, 2.0f), 0.0f);
    EXPECT_FLOAT_EQ(mining::arrow_drop(10.0f, 0.0f), 0.0f); // degenerate speed
    // Drop grows quadratically: twice as far = four times the drop.
    EXPECT_NEAR(mining::arrow_drop(20.0f, 2.0f),
                4.0f * mining::arrow_drop(10.0f, 2.0f), 0.001f);
    // Skeleton arrow speed 1.4*? player speed dominates; faster = flatter.
    EXPECT_LT(mining::arrow_drop(10.0f, mining::bow_speed(1.0f)),
              mining::arrow_drop(10.0f, mining::bow_speed(0.25f)));
}

TEST(BowTest, ItemClassification) {
    EXPECT_TRUE(mining::is_bow(ITEM_BOW));
    EXPECT_FALSE(mining::is_bow(ITEM_STICK));
    EXPECT_TRUE(mining::is_arrow(ITEM_ARROW));
    EXPECT_FALSE(mining::is_arrow(ITEM_CHARCOAL));
}

// ---- Resource loop ----

TEST(BowTest, TallGrassDropsItselfForFiber) {
    auto d = mining::drop_for(BLOCK_TALL_GRASS, ITEM_AIR);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->item, static_cast<ItemId>(BLOCK_TALL_GRASS));
    EXPECT_EQ(d->count_min, 1);
}

TEST(BowTest, BowHasVanillaStyleDurability) {
    const auto& props = ItemRegistry::get(ITEM_BOW);
    EXPECT_EQ(props.max_stack_size, 1);
    EXPECT_EQ(props.max_damage, 384);
    EXPECT_EQ(ItemRegistry::get(ITEM_ARROW).max_stack_size, 64);
}

} // namespace mc

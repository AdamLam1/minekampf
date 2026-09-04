#include <gtest/gtest.h>
#include "gameplay/survival.hpp"

namespace mc {

namespace {
Player fresh_player() { return Player{}; }
} // namespace

// ---- Exhaustion -> saturation -> hunger ----

TEST(SurvivalTest, ExhaustionDrainsSaturationBeforeFood) {
    Player p = fresh_player();
    p.food_level = 20;
    p.food_saturation = 2.0f;
    p.food_exhaustion = 0.0f;

    survival::add_exhaustion(p, 4.0f); // one point
    EXPECT_FLOAT_EQ(p.food_saturation, 1.0f);
    EXPECT_EQ(p.food_level, 20);

    survival::add_exhaustion(p, 8.0f); // two more points: sat->0 then food-1
    EXPECT_FLOAT_EQ(p.food_saturation, 0.0f);
    EXPECT_EQ(p.food_level, 19);
}

TEST(SurvivalTest, SprintingDrainsSlowly) {
    Player p = fresh_player();
    p.mode = GameMode::Survival;
    p.food_saturation = 0.0f;
    int start_food = p.food_level;

    survival::HungerTickInput sprinting{true, false};
    // 150 ticks * 0.02 = 3.0 exhaustion < 4.0 threshold -> no food lost yet.
    for (int i = 0; i < 150; ++i) survival::tick_hunger(p, sprinting);
    EXPECT_EQ(p.food_level, start_food);

    for (int i = 150; i < 210; ++i) survival::tick_hunger(p, sprinting); // +1.2 => crosses 4.0
    EXPECT_LT(p.food_level, start_food);
}

// ---- Natural regeneration ----

TEST(SurvivalTest, RegenHealsWhileFedAndCostsExhaustion) {
    Player p = fresh_player();
    p.health = 10.0f;
    p.food_level = 18;
    p.food_saturation = 5.0f;
    p.mode = GameMode::Survival;

    survival::HungerTickInput idle{};
    for (int i = 0; i < survival::REGEN_INTERVAL_TICKS; ++i) survival::tick_hunger(p, idle);

    EXPECT_FLOAT_EQ(p.health, 11.0f);
    // REGEN_EXHAUSTION (6.0) crossed the 4.0 threshold once.
    EXPECT_TRUE(p.food_saturation < 5.0f || p.food_level < 18);
}

TEST(SurvivalTest, NoRegenWhenHungry) {
    Player p = fresh_player();
    p.health = 10.0f;
    p.food_level = 17; // below the regen gate
    p.mode = GameMode::Survival;

    survival::HungerTickInput idle{};
    for (int i = 0; i < 200; ++i) survival::tick_hunger(p, idle);
    EXPECT_FLOAT_EQ(p.health, 10.0f);
}

// ---- Starvation ----

TEST(SurvivalTest, StarvationStopsAtOneHeartNonHardcore) {
    Player p = fresh_player();
    p.health = 20.0f;
    p.food_level = 0;
    p.mode = GameMode::Survival;

    survival::HungerTickInput idle{};
    for (int i = 0; i < 4000; ++i) survival::tick_hunger(p, idle);
    EXPECT_FLOAT_EQ(p.health, survival::STARVATION_FLOOR);
}

TEST(SurvivalTest, HardcoreStarvationKills) {
    Player p = fresh_player();
    p.health = 5.0f;
    p.food_level = 0;
    p.mode = GameMode::Hardcore;

    survival::HungerTickInput idle{};
    for (int i = 0; i < 10000; ++i) survival::tick_hunger(p, idle);
    EXPECT_FLOAT_EQ(p.health, 0.0f);
}

TEST(SurvivalTest, CreativeIsImmuneToHunger) {
    Player p = fresh_player();
    p.health = 10.0f;
    p.food_level = 0;
    p.mode = GameMode::Creative;

    survival::HungerTickInput sprinting{true, true};
    for (int i = 0; i < 500; ++i) survival::tick_hunger(p, sprinting);
    EXPECT_FLOAT_EQ(p.health, 10.0f);
    EXPECT_EQ(p.food_level, 0);
    EXPECT_FLOAT_EQ(p.food_exhaustion, 0.0f);
}

// ---- XP progression (uses PHASE17 curve from combat.hpp) ----

TEST(SurvivalTest, XpAccumulatesAcrossLevelsExactly) {
    Player p = fresh_player();

    survival::gain_xp(p, 7); // exactly the level-0 requirement (2*0+7)
    EXPECT_EQ(p.xp_total, 7);
    EXPECT_EQ(p.xp_level, 1);
    EXPECT_FLOAT_EQ(p.xp_progress, 0.0f);

    // Level 1 needs 9; grant 5 -> 5/9 progress.
    survival::gain_xp(p, 5);
    EXPECT_EQ(p.xp_total, 12);
    EXPECT_EQ(p.xp_level, 1);
    EXPECT_NEAR(p.xp_progress, 5.0f / 9.0f, 0.001f);

    // +9 more pushes through to level 3 (needs 11 there).
    survival::gain_xp(p, 9);
    EXPECT_EQ(p.xp_level, 2);
    survival::gain_xp(p, 4);
    survival::gain_xp(p, 7);
    EXPECT_EQ(p.xp_level, 3);
    EXPECT_NEAR(p.xp_progress, 5.0f / 13.0f, 0.001f); // 16 crossed 11 -> 5 left over
    EXPECT_EQ(p.xp_total, 32); // 7+5+9+4+7
}

TEST(SurvivalTest, BigXpGrantJumpsManyLevelsAtOnce) {
    Player p = fresh_player();
    survival::gain_xp(p, 500);
    EXPECT_GT(p.xp_level, 3);
    EXPECT_GE(p.xp_progress, 0.0f);
    EXPECT_LE(p.xp_progress, 1.0f);

    // Independent recomputation of expected level/progress.
    int total = 500, lvl = 0;
    while (total >= mc::xp_for_next_level(lvl)) {
        total -= mc::xp_for_next_level(lvl);
        ++lvl;
    }
    EXPECT_EQ(p.xp_level, lvl);
    EXPECT_NEAR(p.xp_progress, static_cast<float>(total) / mc::xp_for_next_level(lvl), 0.02f);
}

TEST(SurvivalTest, ZeroAndNegativeXpAreNoops) {
    Player p = fresh_player();
    survival::gain_xp(p, 10);
    int total_before = p.xp_total;
    survival::gain_xp(p, 0);
    survival::gain_xp(p, -5);
    EXPECT_EQ(p.xp_total, total_before);
}

// ---- Eating ----

TEST(SurvivalTest, EatingConsumesStackAndRestoresHunger) {
    Player p = fresh_player();
    p.food_level = 10;
    p.inventory.set_slot(0, ItemStack(ITEM_APPLE, 2));
    p.inventory.set_selected_hotbar_slot(0);

    int ate = survival::eat_from_selected(p);
    EXPECT_EQ(ate, 4);
    EXPECT_EQ(p.food_level, 14);
    EXPECT_EQ(p.inventory.get_slot(0).count, 1);

    ate = survival::eat_from_selected(p);
    EXPECT_EQ(ate, 4);
    EXPECT_EQ(p.food_level, 18);
    EXPECT_TRUE(p.inventory.get_slot(0).is_empty());
}

TEST(SurvivalTest, EatingCapsAtVanillaMaximums) {
    Player p = fresh_player();
    p.food_level = 19;
    p.food_saturation = 0.0f;
    p.inventory.set_slot(0, ItemStack(ITEM_RAW_MEAT, 4));
    p.inventory.set_selected_hotbar_slot(0);

    survival::eat_from_selected(p);
    EXPECT_EQ(p.food_level, 20); // clamped, not overflowed to 22
    EXPECT_LE(p.food_saturation, 20.0f);
}

TEST(SurvivalTest, NonFoodReturnsZeroWithoutConsume) {
    Player p = fresh_player();
    p.inventory.set_slot(0, ItemStack(ITEM_STICK, 5));
    p.inventory.set_selected_hotbar_slot(0);
    EXPECT_EQ(survival::eat_from_selected(p), 0);
    EXPECT_EQ(p.inventory.get_slot(0).count, 5);
}

} // namespace mc

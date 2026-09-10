#include <gtest/gtest.h>
#include "gameplay/survival.hpp"

namespace mc {

namespace {
Player fresh_player() {
    Player p;
    p.mode = GameMode::Survival;
    return p;
}
} // namespace

// ---- Fall damage ----

TEST(EnvironmentTest, FallUnderThreeBlocksIsFree) {
    Player p = fresh_player();
    survival::EnvironmentTickInput in;
    in.fall_speed = 0.8f;
    for (int i = 0; i < 3; ++i) survival::tick_environment(p, in); // 2.4 blocks
    in.fall_speed = 0.0f;
    in.just_landed = true;
    auto r = survival::tick_environment(p, in);
    EXPECT_FLOAT_EQ(r.fall_damage, 0.0f);
}

TEST(EnvironmentTest, FallDamageVanillaCurve) {
    Player p = fresh_player();
    survival::EnvironmentTickInput in;
    in.fall_speed = 1.0f;
    for (int i = 0; i < 7; ++i) survival::tick_environment(p, in); // 7 blocks
    in.fall_speed = 0.0f;
    in.just_landed = true;
    auto r = survival::tick_environment(p, in);
    EXPECT_FLOAT_EQ(r.fall_damage, 4.0f); // 7 - 3
    EXPECT_FLOAT_EQ(p.fall_distance, 0.0f);
}

TEST(EnvironmentTest, WaterResetsFallTracking) {
    Player p = fresh_player();
    survival::EnvironmentTickInput in;
    in.fall_speed = 1.0f;
    for (int i = 0; i < 10; ++i) survival::tick_environment(p, in);
    in.in_water = true;
    in.fall_speed = 0.0f;
    survival::tick_environment(p, in);
    in.in_water = false;
    in.just_landed = true;
    auto r = survival::tick_environment(p, in);
    EXPECT_FLOAT_EQ(r.fall_damage, 0.0f);
}

TEST(EnvironmentTest, CreativeNeverTakesHazardDamage) {
    Player p = fresh_player();
    p.mode = GameMode::Creative;
    survival::EnvironmentTickInput in;
    in.fall_speed = 3.0f;
    in.head_in_water = true;
    in.body_in_lava = true;
    for (int i = 0; i < 400; ++i) survival::tick_environment(p, in);
    in.creative_exempt = true;
    in.fall_speed = 0.0f;
    in.just_landed = true;
    auto r = survival::tick_environment(p, in);
    EXPECT_FLOAT_EQ(r.total_damage(), 0.0f);
    EXPECT_EQ(p.breath, 300);
}

// ---- Drowning ----

TEST(EnvironmentTest, DrowningStartsAfterAirRunsOut) {
    Player p = fresh_player();
    survival::EnvironmentTickInput in;
    in.head_in_water = true;
    int damage_ticks = 0;
    for (int i = 0; i < 300 + 100; ++i) {
        auto r = survival::tick_environment(p, in);
        if (r.drown_damage > 0.0f) ++damage_ticks;
    }
    // 300 ticks of air, then 1 damage per 20 ticks => 5 damage events in 100 ticks.
    EXPECT_EQ(damage_ticks, 5);
}

TEST(EnvironmentTest, AirRefillsInstantlyOutOfWater) {
    Player p = fresh_player();
    survival::EnvironmentTickInput in;
    in.head_in_water = true;
    for (int i = 0; i < 250; ++i) survival::tick_environment(p, in);
    EXPECT_LT(p.breath, 300);
    in.head_in_water = false;
    survival::tick_environment(p, in);
    EXPECT_EQ(p.breath, 300);
}

// ---- Lava ----

TEST(EnvironmentTest, LavaBurnsTwoHpPerSecond) {
    Player p = fresh_player();
    survival::EnvironmentTickInput in;
    in.body_in_lava = true;
    float total = 0.0f;
    for (int i = 0; i < 40; ++i) {
        auto r = survival::tick_environment(p, in);
        total += r.lava_damage;
    }
    EXPECT_FLOAT_EQ(total, 4.0f); // 2 s in lava => 2 burn ticks * 2 HP
}

} // namespace mc

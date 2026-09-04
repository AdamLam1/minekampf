#include <gtest/gtest.h>
#include "gameplay/smelting.hpp"
#include "save/level_storage.hpp"

#include <filesystem>

namespace mc {

namespace {
// Drives a furnace for `ticks` ticks, counting completed smelts.
int run(smelting::FurnaceState& f, int ticks) {
    int done = 0;
    for (int i = 0; i < ticks; ++i) {
        if (smelting::tick_furnace(f)) ++done;
    }
    return done;
}
} // namespace

// ---- Recipe mapping ----

TEST(SmeltingTest, RecipeMapping) {
    EXPECT_EQ(smelting::smelt_result(static_cast<ItemId>(BLOCK_IRON_ORE))->output, ITEM_IRON_INGOT);
    EXPECT_EQ(smelting::smelt_result(static_cast<ItemId>(BLOCK_GOLD_ORE))->output, ITEM_GOLD_INGOT);
    EXPECT_EQ(smelting::smelt_result(static_cast<ItemId>(BLOCK_SAND))->output,
              static_cast<ItemId>(BLOCK_GLASS));
    EXPECT_EQ(smelting::smelt_result(static_cast<ItemId>(BLOCK_COBBLESTONE))->output, BLOCK_STONE);
    EXPECT_EQ(smelting::smelt_result(ITEM_RAW_MEAT)->output, ITEM_COOKED_MEAT);
    EXPECT_EQ(smelting::smelt_result(ITEM_OAK_LOG)->output, ITEM_CHARCOAL);
    EXPECT_EQ(smelting::smelt_result(static_cast<ItemId>(BLOCK_SPRUCE_LOG))->output, ITEM_CHARCOAL);
    EXPECT_EQ(smelting::smelt_result(static_cast<ItemId>(BLOCK_BIRCH_LOG))->output, ITEM_CHARCOAL);

    EXPECT_FALSE(smelting::smelt_result(ITEM_DIRT).has_value());
    EXPECT_FALSE(smelting::smelt_result(ITEM_STICK).has_value());
}

TEST(SmeltingTest, CookTimeIsVanillaTenSeconds) {
    EXPECT_EQ(smelting::COOK_TICKS, 200);
    auto r = smelting::smelt_result(ITEM_RAW_MEAT);
    ASSERT_TRUE(r.has_value());
    EXPECT_GT(r->xp, 0.0f);
}

// ---- Fuel matching ----

TEST(SmeltingTest, FuelValues) {
    EXPECT_EQ(smelting::fuel_burn_ticks(ITEM_COAL), 1600);
    EXPECT_EQ(smelting::fuel_burn_ticks(ITEM_CHARCOAL), 1600);
    EXPECT_EQ(smelting::fuel_burn_ticks(ITEM_OAK_PLANKS), 300);
    EXPECT_EQ(smelting::fuel_burn_ticks(ITEM_STICK), 100);
    EXPECT_EQ(smelting::fuel_burn_ticks(ITEM_OAK_LOG), 300);
    EXPECT_EQ(smelting::fuel_burn_ticks(ITEM_DIRT), 0);  // not a fuel
    EXPECT_EQ(smelting::fuel_burn_ticks(ITEM_COBBLESTONE), 0);
    EXPECT_EQ(smelting::fuel_burn_ticks(ITEM_AIR), 0);
}

TEST(SmeltingTest, CoalSmeltsEightItems) {
    smelting::FurnaceState f;
    f.input = ItemStack(static_cast<ItemId>(BLOCK_IRON_ORE), 8);
    f.fuel = ItemStack(ITEM_COAL, 1);
    int done = run(f, 1600);
    EXPECT_EQ(done, 8);
    EXPECT_EQ(f.output.item, ITEM_IRON_INGOT);
    EXPECT_EQ(f.output.count, 8);
    EXPECT_TRUE(f.fuel.is_empty());   // exactly one coal burned
    EXPECT_TRUE(f.input.is_empty());  // all ore consumed
}

// ---- State machine ----

TEST(SmeltingTest, NoFuelNoSmelt) {
    smelting::FurnaceState f;
    f.input = ItemStack(static_cast<ItemId>(BLOCK_IRON_ORE), 2);
    int done = run(f, 400);
    EXPECT_EQ(done, 0);
    EXPECT_EQ(f.cook_progress, 0);
    EXPECT_EQ(f.input.count, 2); // ore untouched
    EXPECT_FALSE(f.burning());
}

TEST(SmeltingTest, FuelNotWastedWithoutSmeltableInput) {
    smelting::FurnaceState f;
    f.input = ItemStack(ITEM_DIRT, 5); // dirt has no recipe
    f.fuel = ItemStack(ITEM_COAL, 1);
    int done = run(f, 500);
    EXPECT_EQ(done, 0);
    EXPECT_EQ(f.fuel.count, 1); // coal preserved
    EXPECT_FALSE(f.burning());
}

TEST(SmeltingTest, ProgressResetsWhenFuelRunsOut) {
    smelting::FurnaceState f;
    f.input = ItemStack(static_cast<ItemId>(BLOCK_IRON_ORE), 5);
    f.fuel = ItemStack(ITEM_STICK, 1); // 100 ticks of burn < 200 cook time
    run(f, 100);                       // burns out mid-smelt
    EXPECT_FALSE(f.burning());
    EXPECT_EQ(f.cook_progress, 100);   // half-smelted when flame died

    int done = run(f, 200);            // unlit: progress drains 2/tick
    EXPECT_EQ(done, 0);
    EXPECT_EQ(f.cook_progress, 0);     // no partial carry-over
    EXPECT_EQ(f.input.count, 5);       // ore preserved
}

TEST(SmeltingTest, OutputStacksUpToMax) {
    smelting::FurnaceState f;
    f.input = ItemStack(ITEM_RAW_MEAT, 64);
    f.fuel = ItemStack(ITEM_COAL, 8); // 8 * 1600 = 64 items
    int done = run(f, 64 * smelting::COOK_TICKS);
    EXPECT_EQ(done, 64);
    EXPECT_EQ(f.output.item, ITEM_COOKED_MEAT);
    EXPECT_EQ(f.output.count, 64);
}

TEST(SmeltingTest, OutputFullPausesSmelting) {
    smelting::FurnaceState f;
    f.output = ItemStack(ITEM_IRON_INGOT, 64); // full output slot
    f.input = ItemStack(static_cast<ItemId>(BLOCK_IRON_ORE), 4);
    f.fuel = ItemStack(ITEM_COAL, 2);
    // The furnace is lit and burns fuel, but nothing completes and no ore
    // is consumed while the output slot is full.
    run(f, 400);
    EXPECT_EQ(f.output.count, 64);
    EXPECT_EQ(f.input.count, 4);
    EXPECT_EQ(f.cook_progress, 0);
}

TEST(SmeltingTest, PendingXpAccumulatesPerSmelt) {
    smelting::FurnaceState f;
    f.input = ItemStack(static_cast<ItemId>(BLOCK_IRON_ORE), 3);
    f.fuel = ItemStack(ITEM_COAL, 1);
    run(f, 600);
    auto recipe = smelting::smelt_result(static_cast<ItemId>(BLOCK_IRON_ORE));
    ASSERT_TRUE(recipe.has_value());
    EXPECT_NEAR(f.pending_xp, 3 * recipe->xp, 0.001f);
}

TEST(SmeltingTest, NewFuelIgnitesOnlyWhenSmeltable) {
    smelting::FurnaceState f;
    f.fuel = ItemStack(ITEM_COAL, 1);
    int wasted = run(f, 100);
    EXPECT_EQ(wasted, 0);
    EXPECT_FALSE(f.burning());
    // Add smeltable input: ignites on the next tick.
    f.input = ItemStack(ITEM_RAW_MEAT, 1);
    run(f, 1);
    EXPECT_TRUE(f.burning());
}

// ---- Persistence round-trip (furnaces.dat via LevelStorage) ----

TEST(SmeltingTest, FurnaceStateSurvivesSaveLoad) {
    namespace fs = std::filesystem;
    std::string dir = "test_world_furnaces";
    fs::remove_all(dir);
    fs::create_directories(dir);

    {
        LevelStorage storage(dir);
        std::vector<FurnaceSave> out;
        FurnaceSave f;
        f.dim = 1; f.x = -12; f.y = 70; f.z = 33;
        f.in_item = static_cast<int32_t>(BLOCK_IRON_ORE); f.in_count = 3;
        f.fuel_item = ITEM_COAL; f.fuel_count = 5;
        f.out_item = ITEM_IRON_INGOT; f.out_count = 1;
        f.burn_left = 900; f.burn_total = 1600; f.cook = 123;
        f.pending_xp = 0.7f;
        out.push_back(f);
        storage.save_furnaces(out);
    }
    {
        LevelStorage storage(dir); // fresh instance = fresh caches
        auto loaded = storage.load_furnaces();
        ASSERT_EQ(loaded.size(), 1u);
        const auto& f = loaded[0];
        EXPECT_EQ(f.dim, 1);
        EXPECT_EQ(f.x, -12); EXPECT_EQ(f.y, 70); EXPECT_EQ(f.z, 33);
        EXPECT_EQ(f.in_item, static_cast<int32_t>(BLOCK_IRON_ORE));
        EXPECT_EQ(f.in_count, 3);
        EXPECT_EQ(f.fuel_item, ITEM_COAL);
        EXPECT_EQ(f.fuel_count, 5);
        EXPECT_EQ(f.out_item, ITEM_IRON_INGOT);
        EXPECT_EQ(f.out_count, 1);
        EXPECT_EQ(f.burn_left, 900);
        EXPECT_EQ(f.burn_total, 1600);
        EXPECT_EQ(f.cook, 123);
        EXPECT_NEAR(f.pending_xp, 0.7f, 0.0001f);
    }
    fs::remove_all(dir);
}

TEST(SmeltingTest, MissingFurnaceFileLoadsEmpty) {
    namespace fs = std::filesystem;
    std::string dir = "test_world_furnaces_empty";
    fs::remove_all(dir);
    fs::create_directories(dir);
    LevelStorage storage(dir);
    EXPECT_TRUE(storage.load_furnaces().empty());
    fs::remove_all(dir);
}

} // namespace mc

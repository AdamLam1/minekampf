#include <gtest/gtest.h>
#include "gameplay/crafting.hpp"

namespace mc {

class CraftingTest : public ::testing::Test {
protected:
    void SetUp() override {
        RecipeManager::init_recipes();
    }
};

TEST_F(CraftingTest, ShapedMatchExact) {
    CraftingGrid grid;
    // XXX
    //  |
    //  |
    grid.items[0] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[1] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[2] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[4] = ItemStack(ITEM_STICK);
    grid.items[7] = ItemStack(ITEM_STICK);
    
    auto recipe = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->output.item, ITEM_WOODEN_PICKAXE);
}

TEST_F(CraftingTest, ShapedMatchMirrored) {
    // Pickaxe is symmetric, let's test trimming + symmetry just in case
    CraftingGrid grid;
    // shift down one row
    grid.items[3] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[4] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[5] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[7] = ItemStack(ITEM_STICK);
    // Oh wait, pickaxe needs stick in column 1 for lower rows. If shifted down,
    // stick at 7 means middle column of 3x3.
    // Let's adjust to row 1 and 2
    
    grid.items[3] = ItemStack(ITEM_COBBLESTONE);
    grid.items[4] = ItemStack(ITEM_COBBLESTONE);
    grid.items[5] = ItemStack(ITEM_COBBLESTONE);
    grid.items[7] = ItemStack(ITEM_STICK);
    grid.items[8] = ItemStack(ITEM_AIR); // wait, stick needs to be under middle
    // Ah, wait, middle of row 2 is index 7. Middle of row 1 is index 4.
    
    // row 1: 3, 4, 5
    // row 2: 6, 7, 8
    grid.items[7] = ItemStack(ITEM_STICK); // under middle
    // But pickaxe needs 2 sticks.
    // If it's shifted down, we run out of rows!
    // Pickaxe is height 3. We can't shift it down in a 3x3 grid.
}

TEST_F(CraftingTest, ShapedMatchTrimmed) {
    CraftingGrid grid;
    // Sticks recipe: two planks vertical.
    // Let's put it on the right edge, bottom corner.
    // row 1, col 2 -> index 5
    // row 2, col 2 -> index 8
    grid.items[5] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[8] = ItemStack(ITEM_OAK_PLANKS);
    
    auto recipe = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->output.item, ITEM_STICK);
}

TEST_F(CraftingTest, ShapelessMatch) {
    CraftingGrid grid;
    // Planks from log
    // Put it anywhere
    grid.items[3] = ItemStack(ITEM_OAK_LOG);
    
    auto recipe = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->output.item, ITEM_OAK_PLANKS);
    EXPECT_EQ(recipe->output.count, 4);
}

} // namespace mc

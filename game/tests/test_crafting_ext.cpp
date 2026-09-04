#include <gtest/gtest.h>
#include "gameplay/crafting.hpp"
#include "world/block.hpp"

namespace mc {

// Regression coverage for the expanded survival-crafting recipe set.

class CraftingExtTest : public ::testing::Test {
protected:
    void SetUp() override { RecipeManager::init_recipes(); }
};

TEST_F(CraftingExtTest, CraftingTableFromFourPlanks) {
    CraftingGrid grid;
    grid.items[0] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[1] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[3] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[4] = ItemStack(ITEM_OAK_PLANKS);

    auto r = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->output.item, static_cast<ItemId>(BLOCK_CRAFTING_TABLE));
}

TEST_F(CraftingExtTest, CraftingTableRejectsWrongLayout) {
    CraftingGrid grid;
    // L-shape is not a table.
    grid.items[0] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[1] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[3] = ItemStack(ITEM_OAK_PLANKS);

    auto r = RecipeManager::find_matching_recipe(grid);
    EXPECT_FALSE(r.has_value());
}

TEST_F(CraftingExtTest, TorchFromCoalOverStick) {
    CraftingGrid grid;
    grid.items[1] = ItemStack(ITEM_COAL);  // top
    grid.items[4] = ItemStack(ITEM_STICK); // underneath

    auto r = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->output.item, static_cast<ItemId>(BLOCK_TORCH));
    EXPECT_EQ(r->output.count, 4);
}

TEST_F(CraftingExtTest, SwordRecipeVerticalColumn) {
    CraftingGrid grid;
    grid.items[1] = ItemStack(ITEM_COBBLESTONE);   // blade
    grid.items[4] = ItemStack(ITEM_COBBLESTONE);   // blade
    grid.items[7] = ItemStack(ITEM_STICK);         // handle

    auto r = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->output.item, ITEM_STONE_SWORD);

    // Mirrored column also matches (trim + mirror symmetry).
    CraftingGrid mirrored;
    mirrored.items[1] = ItemStack(ITEM_COBBLESTONE);
    mirrored.items[5] = ItemStack(ITEM_COBBLESTONE);
    mirrored.items[8] = ItemStack(ITEM_STICK);
    // That's an L, not a straight line -> must NOT match.
    auto bad = RecipeManager::find_matching_recipe(mirrored);
    EXPECT_FALSE(bad.has_value());
}

TEST_F(CraftingExtTest, DiamondPickaxeFromDiamondsAndSticks) {
    CraftingGrid grid;
    grid.items[0] = ItemStack(ITEM_DIAMOND);
    grid.items[1] = ItemStack(ITEM_DIAMOND);
    grid.items[2] = ItemStack(ITEM_DIAMOND);
    grid.items[4] = ItemStack(ITEM_STICK);
    grid.items[7] = ItemStack(ITEM_STICK);

    auto r = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->output.item, ITEM_DIAMOND_PICKAXE);
}

TEST_F(CraftingExtTest, IronToolsCraftable) {
    CraftingGrid pick;
    pick.items[0] = ItemStack(ITEM_IRON_INGOT);
    pick.items[1] = ItemStack(ITEM_IRON_INGOT);
    pick.items[2] = ItemStack(ITEM_IRON_INGOT);
    pick.items[4] = ItemStack(ITEM_STICK);
    pick.items[7] = ItemStack(ITEM_STICK);
    ASSERT_TRUE(RecipeManager::find_matching_recipe(pick).has_value());

    CraftingGrid sword;
    sword.items[0] = ItemStack(ITEM_IRON_INGOT);
    sword.items[3] = ItemStack(ITEM_IRON_INGOT);
    sword.items[6] = ItemStack(ITEM_STICK);
    auto s = RecipeManager::find_matching_recipe(sword);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->output.item, ITEM_IRON_SWORD);
}

TEST_F(CraftingExtTest, SpruceAndBirchLogsGivePlanks) {
    const ItemId spruce = static_cast<ItemId>(BLOCK_SPRUCE_LOG);
    const ItemId birch = static_cast<ItemId>(BLOCK_BIRCH_LOG);

    CraftingGrid a;
    a.items[4] = ItemStack(spruce);
    auto ra = RecipeManager::find_matching_recipe(a);
    ASSERT_TRUE(ra.has_value());
    EXPECT_EQ(ra->output.item, ITEM_OAK_PLANKS);

    CraftingGrid b;
    b.items[8] = ItemStack(birch);
    auto rb = RecipeManager::find_matching_recipe(b);
    ASSERT_TRUE(rb.has_value());
    EXPECT_EQ(rb->output.item, ITEM_OAK_PLANKS);
}

TEST_F(CraftingExtTest, BricksFromClayBlock) {
    CraftingGrid grid;
    grid.items[0] = ItemStack(static_cast<ItemId>(BLOCK_CLAY));
    grid.items[1] = ItemStack(static_cast<ItemId>(BLOCK_CLAY));
    grid.items[3] = ItemStack(static_cast<ItemId>(BLOCK_CLAY));
    grid.items[4] = ItemStack(static_cast<ItemId>(BLOCK_CLAY));

    auto r = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->output.item, static_cast<ItemId>(BLOCK_BRICKS));
}

TEST_F(CraftingExtTest, FurnaceFromCobblestoneRing) {
    CraftingGrid grid;
    // 3x3 ring with an empty center (indices 0..8 minus 4).
    const int ring[] = {0, 1, 2, 3, 5, 6, 7, 8};
    for (int idx : ring) grid.items[idx] = ItemStack(ITEM_COBBLESTONE);

    auto r = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->output.item, static_cast<ItemId>(BLOCK_FURNACE));

    // Solid 3x3 of cobblestone must NOT match (center must be empty).
    CraftingGrid solid;
    for (int i = 0; i < 9; ++i) solid.items[i] = ItemStack(ITEM_COBBLESTONE);
    EXPECT_FALSE(RecipeManager::find_matching_recipe(solid).has_value());
}

TEST_F(CraftingExtTest, BowFromSticksAndFiberString) {
    const ItemId fiber = static_cast<ItemId>(BLOCK_TALL_GRASS);
    CraftingGrid grid;
    // "|XX"
    // "| X"
    // "|XX"   (string column + bent stick limbs)
    grid.items[0] = ItemStack(fiber);
    grid.items[1] = ItemStack(ITEM_STICK);
    grid.items[2] = ItemStack(ITEM_STICK);
    grid.items[3] = ItemStack(fiber);
    grid.items[5] = ItemStack(ITEM_STICK);
    grid.items[6] = ItemStack(fiber);
    grid.items[7] = ItemStack(ITEM_STICK);
    grid.items[8] = ItemStack(ITEM_STICK);

    auto r = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->output.item, ITEM_BOW);

    // Mirrored bow also matches (symmetry convention): mirror of
    // "|XX"/"| X"/"|XX" is "XX|"/"X |"/"XX|".
    CraftingGrid mirrored;
    mirrored.items[0] = ItemStack(ITEM_STICK);
    mirrored.items[1] = ItemStack(ITEM_STICK);
    mirrored.items[2] = ItemStack(fiber);
    mirrored.items[3] = ItemStack(ITEM_STICK);
    mirrored.items[5] = ItemStack(fiber);
    mirrored.items[6] = ItemStack(ITEM_STICK);
    mirrored.items[7] = ItemStack(ITEM_STICK);
    mirrored.items[8] = ItemStack(fiber);
    auto rm = RecipeManager::find_matching_recipe(mirrored);
    ASSERT_TRUE(rm.has_value());
    EXPECT_EQ(rm->output.item, ITEM_BOW);
}

TEST_F(CraftingExtTest, ArrowsFromFiberAndStick) {
    const ItemId fiber = static_cast<ItemId>(BLOCK_TALL_GRASS);
    CraftingGrid grid;
    grid.items[1] = ItemStack(fiber);   // fletching
    grid.items[4] = ItemStack(ITEM_STICK); // shaft

    auto r = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->output.item, ITEM_ARROW);
    EXPECT_EQ(r->output.count, 4);
}

TEST_F(CraftingExtTest, ArrowRecipeDoesNotCollideWithTorch) {
    // Torch = coal over stick; arrow = fiber over stick. The other must not
    // match when only one ingredient type is present.
    const ItemId fiber = static_cast<ItemId>(BLOCK_TALL_GRASS);
    CraftingGrid arrow_grid;
    arrow_grid.items[1] = ItemStack(fiber);
    arrow_grid.items[4] = ItemStack(ITEM_STICK);
    auto r = RecipeManager::find_matching_recipe(arrow_grid);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->output.item, ITEM_ARROW); // fiber, not coal -> arrow

    CraftingGrid torch_grid;
    torch_grid.items[1] = ItemStack(ITEM_COAL);
    torch_grid.items[4] = ItemStack(ITEM_STICK);
    auto t = RecipeManager::find_matching_recipe(torch_grid);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->output.item, static_cast<ItemId>(BLOCK_TORCH));
}

TEST_F(CraftingExtTest, PickaxeCannotFitInTwoByTwoSubgrid) {
    // Simulate the inventory crafting view: only indices 0/1/3/4 usable.
    // A full pickaxe row needs three columns -> never matches from 2x2.
    CraftingGrid grid;
    grid.items[0] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[1] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[3] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[4] = ItemStack(ITEM_STICK);

    auto r = RecipeManager::find_matching_recipe(grid);
    EXPECT_FALSE(r.has_value()); // neither pickaxe nor anything else
}

} // namespace mc

#include <gtest/gtest.h>
#include "gameplay/inventory.hpp"

namespace mc {

TEST(Inventory, AddItemToEmpty) {
    Inventory inv(3);
    ItemStack stack(ITEM_STONE, 10);
    
    bool result = inv.add_item(stack);
    EXPECT_TRUE(result);
    EXPECT_EQ(stack.count, 0); // fully consumed
    
    auto slot0 = inv.get_slot(0);
    EXPECT_EQ(slot0.item, ITEM_STONE);
    EXPECT_EQ(slot0.count, 10);
}

TEST(Inventory, Stacking) {
    Inventory inv(3);
    inv.set_slot(0, ItemStack(ITEM_STONE, 60)); // 4 space left
    
    ItemStack stack(ITEM_STONE, 10);
    bool result = inv.add_item(stack);
    
    EXPECT_TRUE(result);
    EXPECT_EQ(stack.count, 0);
    
    EXPECT_EQ(inv.get_slot(0).count, 64);
    EXPECT_EQ(inv.get_slot(1).item, ITEM_STONE);
    EXPECT_EQ(inv.get_slot(1).count, 6);
}

TEST(Inventory, Overflow) {
    Inventory inv(1); // very small
    inv.set_slot(0, ItemStack(ITEM_STONE, 60));
    
    ItemStack stack(ITEM_STONE, 10);
    bool result = inv.add_item(stack);
    
    EXPECT_FALSE(result); // Couldn't fit all
    EXPECT_EQ(stack.count, 6); // 6 left over
    EXPECT_EQ(inv.get_slot(0).count, 64);
}

TEST(PlayerInventory, MainInventoryOnly) {
    PlayerInventory inv;
    // Fill hotbar and main inventory with dirt except last main slot
    for (size_t i = 0; i < 35; ++i) {
        inv.set_slot(i, ItemStack(ITEM_DIRT, 64));
    }
    
    ItemStack stack(ITEM_STONE, 10);
    bool result = inv.add_item_to_main(stack);
    
    EXPECT_TRUE(result);
    EXPECT_EQ(inv.get_slot(35).item, ITEM_STONE);
    EXPECT_EQ(inv.get_slot(35).count, 10);
    
    // Now slot 35 is partially full. Let's fill it.
    inv.set_slot(35, ItemStack(ITEM_STONE, 64));
    
    // Armor slot (36) is empty, but add_item_to_main shouldn't use it
    ItemStack stack2(ITEM_STONE, 1);
    bool result2 = inv.add_item_to_main(stack2);
    
    EXPECT_FALSE(result2);
    EXPECT_EQ(stack2.count, 1);
    EXPECT_TRUE(inv.get_slot(36).is_empty());
}

} // namespace mc

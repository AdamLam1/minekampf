#pragma once

#include <vector>
#include "gameplay/item.hpp"

namespace mc {

class Inventory {
public:
    explicit Inventory(size_t size);
    virtual ~Inventory() = default;

    [[nodiscard]] size_t size() const { return slots_.size(); }
    
    [[nodiscard]] ItemStack get_slot(size_t index) const;
    void set_slot(size_t index, const ItemStack& stack);
    
    // Tries to add an item. Returns true if fully added, false if inventory full.
    // The input stack is modified (count reduced by amount successfully added).
    bool add_item(ItemStack& stack);
    
    // Moves items from one slot to another.
    // If amount is 0, moves as much as possible.
    // If the target slot contains a different item, and we are moving the whole stack, they are swapped.
    // Returns true if the operation was successful (at least one item moved or swapped).
    bool move_item(size_t from_index, size_t to_index, uint8_t amount = 0);
    
    [[nodiscard]] bool is_empty() const;
    
    void clear();

    // Move up to `amount` items from `from_index` in this inventory to `to_index` in `target` inventory.
    // Returns the number of items successfully moved.
    uint8_t move_item(size_t from_index, Inventory& target, size_t to_index, uint8_t amount = 64);

    // Swap the contents of two slots within this inventory.
    void swap_slots(size_t index1, size_t index2);

protected:
    std::vector<ItemStack> slots_;
    bool dirty_ = true;
};

class PlayerInventory : public Inventory {
public:
    PlayerInventory();
    
    // 0-8: Hotbar
    // 9-35: Main Inventory
    // 36-39: Armor (Boots, Leggings, Chestplate, Helmet)
    // 40: Off-hand
    // 41: Crafting Output
    // 42-45: Crafting Input (2x2)
    
    void set_selected_hotbar_slot(uint8_t slot) { selected_hotbar_slot_ = slot % 9; }
    [[nodiscard]] uint8_t get_selected_hotbar_slot() const { return selected_hotbar_slot_; }
    
    [[nodiscard]] ItemStack get_selected_item() const { return get_slot(selected_hotbar_slot_); }
    
    // Custom add_item that only checks hotbar and main inventory (slots 0-35)
    bool add_item_to_main(ItemStack& stack);

private:
    uint8_t selected_hotbar_slot_ = 0;
};

} // namespace mc

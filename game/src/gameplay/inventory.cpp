#include "gameplay/inventory.hpp"
#include <algorithm>

namespace mc {

Inventory::Inventory(size_t size) : slots_(size) {}

ItemStack Inventory::get_slot(size_t index) const {
    if (index >= slots_.size()) return {};
    return slots_[index];
}

void Inventory::set_slot(size_t index, const ItemStack& stack) {
    if (index >= slots_.size()) return;
    slots_[index] = stack;
    dirty_ = true;
}

bool Inventory::add_item(ItemStack& stack) {
    if (stack.is_empty()) return true;

    // First pass: try to stack with existing items
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].can_stack_with(stack)) {
            uint8_t max_stack = slots_[i].max_stack_size();
            if (slots_[i].count < max_stack) {
                uint8_t space = max_stack - slots_[i].count;
                uint8_t transfer = std::min(space, stack.count);
                slots_[i].count += transfer;
                stack.count -= transfer;
                dirty_ = true;
                
                if (stack.count == 0) {
                    stack.item = ITEM_AIR;
                    return true;
                }
            }
        }
    }

    // Second pass: place in empty slots
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].is_empty()) {
            slots_[i] = stack;
            stack.count = 0;
            stack.item = ITEM_AIR;
            dirty_ = true;
            return true;
        }
    }

    return false; // Inventory full, could not add all items
}

bool Inventory::move_item(size_t from_index, size_t to_index, uint8_t amount) {
    if (from_index >= slots_.size() || to_index >= slots_.size() || from_index == to_index) return false;

    ItemStack& from_stack = slots_[from_index];
    ItemStack& to_stack = slots_[to_index];

    if (from_stack.is_empty()) return false;

    uint8_t move_count = (amount == 0 || amount > from_stack.count) ? from_stack.count : amount;

    if (to_stack.is_empty()) {
        to_stack = from_stack;
        to_stack.count = move_count;
        from_stack.count -= move_count;
        if (from_stack.count == 0) from_stack.item = ITEM_AIR;
        dirty_ = true;
        return true;
    }

    if (to_stack.can_stack_with(from_stack)) {
        uint8_t max_stack = to_stack.max_stack_size();
        if (to_stack.count >= max_stack) return false;

        uint8_t space = max_stack - to_stack.count;
        uint8_t transfer = std::min(space, move_count);
        to_stack.count += transfer;
        from_stack.count -= transfer;
        if (from_stack.count == 0) from_stack.item = ITEM_AIR;
        dirty_ = true;
        return true;
    }

    // Different items. We can only swap if we are trying to move the whole stack from 'from_index'.
    if (move_count == from_stack.count) {
        std::swap(from_stack, to_stack);
        dirty_ = true;
        return true;
    }

    return false;
}

bool Inventory::is_empty() const {
    for (const auto& slot : slots_) {
        if (!slot.is_empty()) return false;
    }
    return true;
}

void Inventory::clear() {
    for (auto& slot : slots_) {
        slot = {};
    }
    dirty_ = true;
}

uint8_t Inventory::move_item(size_t from_index, Inventory& target, size_t to_index, uint8_t amount) {
    if (from_index >= slots_.size() || to_index >= target.slots_.size() || amount == 0) {
        return 0;
    }

    ItemStack& source_stack = slots_[from_index];
    if (source_stack.is_empty()) {
        return 0;
    }

    ItemStack& target_stack = target.slots_[to_index];
    uint8_t to_move = std::min(source_stack.count, amount);

    // If target is empty, we can just move it
    if (target_stack.is_empty()) {
        target_stack = source_stack;
        target_stack.count = to_move;
        
        source_stack.count -= to_move;
        if (source_stack.count == 0) source_stack.item = ITEM_AIR;
        
        dirty_ = true;
        target.dirty_ = true;
        return to_move;
    }

    // If target has same item, try stacking
    if (target_stack.can_stack_with(source_stack)) {
        uint8_t space = target_stack.max_stack_size() - target_stack.count;
        uint8_t actual_move = std::min(to_move, space);
        
        if (actual_move > 0) {
            target_stack.count += actual_move;
            source_stack.count -= actual_move;
            if (source_stack.count == 0) source_stack.item = ITEM_AIR;
            
            dirty_ = true;
            target.dirty_ = true;
            return actual_move;
        }
    }

    return 0;
}

void Inventory::swap_slots(size_t index1, size_t index2) {
    if (index1 >= slots_.size() || index2 >= slots_.size() || index1 == index2) return;
    std::swap(slots_[index1], slots_[index2]);
    dirty_ = true;
}

// --- PlayerInventory ---

PlayerInventory::PlayerInventory() : Inventory(46) {}

bool PlayerInventory::add_item_to_main(ItemStack& stack) {
    if (stack.is_empty()) return true;

    // Hotbar + Main inventory = 36 slots
    // First pass: try to stack with existing items
    for (size_t i = 0; i < 36; ++i) {
        if (slots_[i].can_stack_with(stack)) {
            uint8_t max_stack = slots_[i].max_stack_size();
            if (slots_[i].count < max_stack) {
                uint8_t space = max_stack - slots_[i].count;
                uint8_t transfer = std::min(space, stack.count);
                slots_[i].count += transfer;
                stack.count -= transfer;
                dirty_ = true;
                
                if (stack.count == 0) {
                    stack.item = ITEM_AIR;
                    return true;
                }
            }
        }
    }

    // Second pass: place in empty slots
    for (size_t i = 0; i < 36; ++i) {
        if (slots_[i].is_empty()) {
            slots_[i] = stack;
            stack.count = 0;
            stack.item = ITEM_AIR;
            dirty_ = true;
            return true;
        }
    }

    return false;
}

} // namespace mc

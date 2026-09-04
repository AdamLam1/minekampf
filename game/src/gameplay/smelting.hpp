#pragma once

// Furnace smelting: recipe mapping, fuel burn values, and the per-tick
// furnace state machine. Pure logic, no engine dependencies — unit tested
// directly (tests/test_smelting).

#include <optional>

#include "gameplay/item.hpp"
#include "world/block.hpp"

namespace mc::smelting {

// Ticks an item takes to smelt (vanilla: 200 = 10 s).
inline constexpr int COOK_TICKS = 200;

struct SmeltResult {
    ItemId output;
    float xp;
};

// Recipe map. Logs of all species smelt into charcoal; ores into ingots
// (closing the iron-tool chain); sand into glass; meat cooks.
[[nodiscard]] inline constexpr std::optional<SmeltResult> smelt_result(ItemId input) {
    switch (input) {
        case BLOCK_IRON_ORE: return SmeltResult{ITEM_IRON_INGOT, 0.7f};
        case BLOCK_GOLD_ORE: return SmeltResult{ITEM_GOLD_INGOT, 1.0f};
        case BLOCK_SAND: return SmeltResult{static_cast<ItemId>(BLOCK_GLASS), 0.1f};
        case BLOCK_COBBLESTONE: return SmeltResult{BLOCK_STONE, 0.1f};
        case ITEM_RAW_MEAT: return SmeltResult{ITEM_COOKED_MEAT, 0.35f};
        case ITEM_OAK_LOG:
        case static_cast<ItemId>(BLOCK_SPRUCE_LOG):
        case static_cast<ItemId>(BLOCK_BIRCH_LOG):
            return SmeltResult{ITEM_CHARCOAL, 0.15f};
        default: return std::nullopt;
    }
}

// Burn duration in ticks per single fuel unit. 0 = not a fuel.
[[nodiscard]] inline constexpr int fuel_burn_ticks(ItemId fuel) {
    switch (fuel) {
        case ITEM_COAL:
        case ITEM_CHARCOAL: return 1600; // 8 items @ 200 ticks
        case ITEM_OAK_PLANKS: return 300;
        case ITEM_OAK_LOG:
        case static_cast<ItemId>(BLOCK_SPRUCE_LOG):
        case static_cast<ItemId>(BLOCK_BIRCH_LOG): return 300;
        case ITEM_STICK: return 100;
        default: return 0;
    }
}

// Full furnace block state (one placed BLOCK_FURNACE).
struct FurnaceState {
    ItemStack input;
    ItemStack fuel;
    ItemStack output;
    int burn_left = 0;        // ticks of burn remaining on current fuel unit
    int burn_total = 0;       // total burn of the fuel unit being consumed
    int cook_progress = 0;    // 0..COOK_TICKS on the current input item
    float pending_xp = 0.0f;  // collected by the player on output pickup

    [[nodiscard]] bool burning() const { return burn_left > 0; }
};

// One furnace tick. Returns true when an item finished smelting this tick.
inline bool tick_furnace(FurnaceState& f) {
    auto recipe = f.input.is_empty() ? std::nullopt : smelt_result(f.input.item);
    bool output_ready = recipe &&
        (f.output.is_empty() ||
         (f.output.can_stack_with(ItemStack(recipe->output)) &&
          f.output.count < f.output.max_stack_size()));

    // Fuel is only consumed while there is something to smelt.
    if (!f.burning() && output_ready && !f.fuel.is_empty()) {
        int burn = fuel_burn_ticks(f.fuel.item);
        if (burn > 0) {
            f.burn_total = burn;
            f.burn_left = burn;
            f.fuel.count -= 1;
            if (f.fuel.count == 0) f.fuel.item = ITEM_AIR;
        }
    }

    if (f.burning()) {
        f.burn_left -= 1;
        if (output_ready) {
            ++f.cook_progress;
            if (f.cook_progress >= COOK_TICKS) {
                f.cook_progress = 0;
                if (f.output.is_empty()) {
                    f.output = ItemStack(recipe->output, 1);
                } else {
                    f.output.count += 1;
                }
                f.pending_xp += recipe->xp;
                f.input.count -= 1;
                if (f.input.count == 0) f.input.item = ITEM_AIR;
                return true;
            }
        } else {
            // Idle flame: progress drains instead of holding a half-cooked item.
            f.cook_progress = (f.cook_progress > 2) ? f.cook_progress - 2 : 0;
        }
    } else {
        f.cook_progress = (f.cook_progress > 2) ? f.cook_progress - 2 : 0;
    }
    return false;
}

} // namespace mc::smelting

#pragma once

#include <cstdint>

#include "gameplay/item.hpp"

namespace mc {

// One enchantment offer shown in the enchanting table UI.
struct EnchantOffer {
    uint8_t xp_cost = 0;     // player levels required
    EnchantType type = EnchantType::Protection;
    uint8_t level = 0;       // enchantment level granted
};

class EnchantingSystem {
public:
    // Calculates the cost (in XP levels) to use an anvil.
    // cost = base_cost + prior_work_penalty + rename_cost (omitted for now).
    // Anvil limit is usually 39. Returns -1 if it's "Too Expensive!".
    static int calculate_anvil_cost(int base_enchantment_cost, int prior_work_penalty, int rename_cost);

    // Calculates the prior work penalty given the number of times the item has been through an anvil.
    // Penalty = 2^n - 1 (Minecraft Java uses 2^n - 1 for repair cost base).
    static int calculate_prior_work_penalty(int prior_uses);

    // Calculate the level of the enchantment offered at the given slot (1, 2, or 3)
    // based on the number of bookshelves around the enchanting table (0 to 15).
    // This is a simplified deterministic mockup based on standard Minecraft math.
    static int calculate_enchantment_slot_level(int slot, int bookshelves);

    // --- Enchanting table flow ---

    // Roll the three offers for a table at table_pos with `bookshelves` nearby.
    // Deterministic per (world_seed, table_pos) so the offers don't flicker
    // between UI redraws but differ between tables.
    static void roll_offers(uint32_t world_seed, uint64_t table_pos_hash, int bookshelves,
                            EnchantOffer out_offers[3]);

    // Whether the offer can be applied: item enchantable, level affordable,
    // and the new level would be an upgrade (or a fresh enchant).
    static bool can_apply(const EnchantOffer& offer, const ItemStack& item, int player_xp_level);

    // Applies the offer to the stack. Returns the modified stack (level max'ed
    // at ENCHANT_MAX_LEVEL); cost deduction is the caller's job.
    static ItemStack apply(const EnchantOffer& offer, ItemStack item);

    // Chooses which enchantment kind a table offers for this item kind
    // (weapons -> Sharpness, pickaxes -> Efficiency, armor-ish -> Protection).
    static EnchantType enchant_for_item(ItemId item);

    // Which enchantment kinds make sense on the item at all.
    static bool is_enchantable(ItemId item);
};

} // namespace mc

#include "gameplay/enchanting.hpp"
#include <cmath>
#include <algorithm>

namespace mc {

int EnchantingSystem::calculate_prior_work_penalty(int prior_uses) {
    if (prior_uses == 0) return 0;
    // penalty = 2^n - 1
    return (1 << prior_uses) - 1;
}

int EnchantingSystem::calculate_anvil_cost(int base_enchantment_cost, int prior_work_penalty, int rename_cost) {
    int total_cost = base_enchantment_cost + prior_work_penalty + rename_cost;
    if (total_cost >= 40) { // Limit is 39, >= 40 means too expensive
        return -1;
    }
    return total_cost;
}

int EnchantingSystem::calculate_enchantment_slot_level(int slot, int bookshelves) {
    // Standard Minecraft limits bookshelves to 15
    int effective_books = std::min(bookshelves, 15);

    int base;
    if (slot == 1) base = 1;
    else if (slot == 2) base = 2;
    else base = 3;

    // Simplified deterministic level formula to reach exactly 30 at 15 books for slot 3
    int level = base * 10 * effective_books / 15 + base;
    return std::max(1, std::min(level, 30));
}

bool EnchantingSystem::is_enchantable(ItemId item) {
    return enchant_for_item(item) != static_cast<EnchantType>(255);
}

EnchantType EnchantingSystem::enchant_for_item(ItemId item) {
    // Swords -> Sharpness, pickaxes -> Efficiency, everything else durable
    // (bow) -> Unbreaking. Returns 255 when the item cannot be enchanted.
    if (item >= ITEM_WOODEN_SWORD && item <= ITEM_DIAMOND_SWORD) return EnchantType::Sharpness;
    if (item >= ITEM_WOODEN_PICKAXE && item <= ITEM_DIAMOND_PICKAXE) return EnchantType::Efficiency;
    if (item == ITEM_BOW) return EnchantType::Unbreaking;
    return static_cast<EnchantType>(255);
}

void EnchantingSystem::roll_offers(uint32_t world_seed, uint64_t table_pos_hash, int bookshelves,
                                   EnchantOffer out_offers[3]) {
    for (int slot = 1; slot <= 3; ++slot) {
        int slot_level = calculate_enchantment_slot_level(slot, bookshelves);
        // XP cost scales with the offered level (Minecraft-ish: 1..30).
        int xp_cost = std::max(1, std::min(slot_level, 30));

        // Pick the enchantment level from the offered power. Higher slots give
        // higher levels of the same kind; mix in the position hash so the kind
        // is stable per table but varies between tables.
        const uint64_t h = (table_pos_hash * 0x9E3779B97F4A7C15ull) ^
                           (world_seed + static_cast<uint32_t>(slot) * 0x85EBCA6Bu);
        const int kind_roll = static_cast<int>((h >> 13) % 100);

        EnchantType kind = EnchantType::Unbreaking;
        if (kind_roll < 70) {
            kind = EnchantType::Sharpness; // replaced per-item by can_apply check
        }

        // Level: divide the slot power across level bands (1-5).
        uint8_t level = static_cast<uint8_t>(std::max(1, std::min((slot_level + 4) / 6,
                                                                  static_cast<int>(ENCHANT_MAX_LEVEL))));

        out_offers[slot - 1] = EnchantOffer{static_cast<uint8_t>(xp_cost), kind, level};
    }
}

bool EnchantingSystem::can_apply(const EnchantOffer& offer, const ItemStack& item, int player_xp_level) {
    if (item.is_empty()) return false;
    if (static_cast<int>(offer.xp_cost) > player_xp_level) return false;
    EnchantType wanted = enchant_for_item(item.item);
    if (static_cast<int>(wanted) == 255) return false;
    if (offer.type != wanted) return false; // offer doesn't fit this item
    return item.enchant_level_of(wanted) < offer.level; // must be an upgrade
}

ItemStack EnchantingSystem::apply(const EnchantOffer& offer, ItemStack item) {
    EnchantType wanted = enchant_for_item(item.item);
    if (static_cast<int>(wanted) != 255 && offer.type == wanted) {
        item.enchant_levels =
            with_enchant_level(item.enchant_levels, wanted, offer.level);
    }
    return item;
}

} // namespace mc

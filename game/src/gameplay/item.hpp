#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace mc {

using ItemId = uint16_t;

inline constexpr ItemId ITEM_AIR = 0;
inline constexpr ItemId ITEM_STONE = 1;
inline constexpr ItemId ITEM_GRASS_BLOCK = 2;
inline constexpr ItemId ITEM_DIRT = 3;
inline constexpr ItemId ITEM_COBBLESTONE = 4;
inline constexpr ItemId ITEM_OAK_PLANKS = 5;
inline constexpr ItemId ITEM_OAK_LOG = 6;
// ... other blocks mapped 1:1 up to ~255 usually

inline constexpr ItemId ITEM_IRON_INGOT = 256;
inline constexpr ItemId ITEM_GOLD_INGOT = 257;
inline constexpr ItemId ITEM_DIAMOND = 258;
inline constexpr ItemId ITEM_STICK = 259;
inline constexpr ItemId ITEM_WOODEN_PICKAXE = 260;
inline constexpr ItemId ITEM_STONE_PICKAXE = 261;
inline constexpr ItemId ITEM_IRON_PICKAXE = 262;
inline constexpr ItemId ITEM_DIAMOND_PICKAXE = 263;
// Survival-loop items (drops, weapons, food).
inline constexpr ItemId ITEM_COAL = 264;
inline constexpr ItemId ITEM_APPLE = 265;
inline constexpr ItemId ITEM_WOODEN_SWORD = 266;
inline constexpr ItemId ITEM_STONE_SWORD = 267;
inline constexpr ItemId ITEM_IRON_SWORD = 268;
inline constexpr ItemId ITEM_DIAMOND_SWORD = 269;
inline constexpr ItemId ITEM_RAW_MEAT = 270;
inline constexpr ItemId ITEM_COOKED_MEAT = 271;
inline constexpr ItemId ITEM_CHARCOAL = 272;
inline constexpr ItemId ITEM_BOW = 273;
inline constexpr ItemId ITEM_ARROW = 274;
inline constexpr ItemId ITEM_BOOK = 275;
inline constexpr ItemId ITEM_COUNT = 276;

struct ItemProperties {
    std::string_view name;
    uint8_t max_stack_size;
    uint16_t max_damage;
};

// Enchantment kinds. Levels are packed into ItemStack::enchant_levels,
// 4 bits per kind (nibble i = level of kind i; 0 = not enchanted).
enum class EnchantType : uint8_t {
    Protection = 0, // armor: reduces incoming damage
    Sharpness = 1,  // weapons: bonus melee damage
    Efficiency = 2, // tools: faster mining
    Unbreaking = 3, // any gear: chance to skip durability loss
};
inline constexpr int ENCHANT_COUNT = 4;
inline constexpr uint8_t ENCHANT_MAX_LEVEL = 5;

[[nodiscard]] inline uint8_t enchant_level(uint16_t packed, EnchantType type) {
    return static_cast<uint8_t>((packed >> (4 * static_cast<int>(type))) & 0xF);
}
[[nodiscard]] inline uint16_t with_enchant_level(uint16_t packed, EnchantType type,
                                                  uint8_t level) {
    if (level > ENCHANT_MAX_LEVEL) level = ENCHANT_MAX_LEVEL;
    const int shift = 4 * static_cast<int>(type);
    return static_cast<uint16_t>((packed & ~(0xFu << shift)) |
                                 (static_cast<unsigned>(level) << shift));
}

class ItemRegistry {
public:
    static const ItemProperties& get(ItemId id);
    // Case-insensitive lookup by registry name; block ids count as items.
    // Returns ITEM_AIR when unknown.
    static ItemId id_from_name(std::string_view name);
    ItemRegistry() = delete;
};

// Simplified ItemStack (no NBT for now to keep it minimal, but supports damage and count)
struct ItemStack {
    ItemId item = ITEM_AIR;
    uint8_t count = 0;
    uint16_t damage = 0;
    // Packed enchantment levels (4 bits per EnchantType, see enchant_level()).
    uint16_t enchant_levels = 0;

    constexpr ItemStack() = default;
    constexpr ItemStack(ItemId i, uint8_t c = 1, uint16_t d = 0) : item(i), count(c), damage(d) {
        if (i == ITEM_AIR) count = 0;
    }

    [[nodiscard]] bool is_empty() const {
        return item == ITEM_AIR || count == 0;
    }

    [[nodiscard]] uint8_t max_stack_size() const {
        if (is_empty()) return 0;
        return ItemRegistry::get(item).max_stack_size;
    }

    [[nodiscard]] bool can_stack_with(const ItemStack& other) const {
        if (is_empty() || other.is_empty()) return false;
        return item == other.item && damage == other.damage &&
               enchant_levels == other.enchant_levels;
    }

    [[nodiscard]] uint8_t enchant_level_of(EnchantType type) const {
        return enchant_level(enchant_levels, type);
    }

    [[nodiscard]] bool is_enchanted() const { return enchant_levels != 0; }

    ItemStack split(uint8_t amount) {
        if (amount > count) amount = count;
        ItemStack new_stack = *this;
        new_stack.count = amount;
        count -= amount;
        if (count == 0) item = ITEM_AIR;
        return new_stack;
    }
};

} // namespace mc

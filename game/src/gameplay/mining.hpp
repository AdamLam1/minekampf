#pragma once

// Pure gameplay-balance logic: block hardness / tool effectiveness / break
// times / block drops / weapon damage / food values. Header-only and free of
// engine dependencies so it can be unit-tested directly (see test_mining).

#include <array>
#include <cstdint>
#include <optional>

#include "gameplay/item.hpp"
#include "world/block.hpp"

namespace mc::mining {

// Tool classes relevant to block breaking.
enum class ToolClass : uint8_t {
    None = 0,
    Pickaxe,
    Axe,
    Shovel,
};

enum class MaterialTier : uint8_t {
    Hand = 0,
    Wood = 1,
    Stone = 2,
    Iron = 3,
    Diamond = 4,
};

struct ToolInfo {
    ToolClass tool_class = ToolClass::None;
    MaterialTier tier = MaterialTier::Hand;
    float speed_multiplier = 1.0f; // applied when tool class is effective
};

// Base seconds to break a block bare-handed with correct drops possible
// (i.e. hardness in seconds for hand mining). 0 = instantly breakable,
// -1 = unbreakable (bedrock-style).
inline constexpr float UNBREAKABLE = -1.0f;

[[nodiscard]] inline constexpr float hardness(BlockId b) {
    switch (b) {
        case BLOCK_AIR:
        case BLOCK_WATER:
        case BLOCK_LAVA:
        case BLOCK_NETHER_PORTAL:
        case BLOCK_END_PORTAL:
        case BLOCK_REDSTONE_WIRE:
            return 0.0f; // not physically breakable, no tool wear
        case BLOCK_STONE:
        case BLOCK_COBBLESTONE:
        case BLOCK_COAL_ORE:
        case BLOCK_GRANITE:
        case BLOCK_DIORITE:
        case BLOCK_ANDESITE:
        case BLOCK_SANDSTONE:
        case BLOCK_NETHERRACK:
        case BLOCK_BRICKS:
            return 1.5f;
        case BLOCK_IRON_ORE:
        case BLOCK_TERRACOTTA:
        case BLOCK_CRAFTING_TABLE:
        case BLOCK_FURNACE:
            return 2.25f;
        case BLOCK_ENCHANTING_TABLE:
            return 5.0f;
        case BLOCK_BOOKSHELF:
            return 1.4f;
        case BLOCK_LEVER_OFF:
        case BLOCK_LEVER_ON:
        case BLOCK_PRESSURE_PLATE_OFF:
        case BLOCK_PRESSURE_PLATE_ON:
        case BLOCK_REPEATER_OFF:
        case BLOCK_REPEATER_ON:
            return 0.5f;
        case BLOCK_REDSTONE_LAMP_OFF:
        case BLOCK_REDSTONE_LAMP_ON:
            return 0.3f;
        case BLOCK_GOLD_ORE:
        case BLOCK_DIAMOND_ORE:
            return 2.5f;
        case BLOCK_QUEST_NPC:
            return UNBREAKABLE; // village fixture: cannot be mined
        case BLOCK_OBSIDIAN:
            return 9.4f;
        case BLOCK_OAK_PLANKS:
        case BLOCK_OAK_LOG:
        case BLOCK_SPRUCE_LOG:
        case BLOCK_BIRCH_LOG:
        case BLOCK_ACACIA_LOG:
        case BLOCK_CACTUS:
            return 1.4f;
        case BLOCK_OAK_LEAVES:
        case BLOCK_SPRUCE_LEAVES:
        case BLOCK_BIRCH_LEAVES:
        case BLOCK_ACACIA_LEAVES:
            return 0.2f;
        case BLOCK_GLASS:
            return 0.3f;
        case BLOCK_DIRT:
            return 0.5f;
        case BLOCK_SAND:
        case BLOCK_GRAVEL:
        case BLOCK_CLAY:
        case BLOCK_SNOW:
            return 0.6f;
        case BLOCK_GRASS:
            return 0.55f;
        case BLOCK_TALL_GRASS:
        case BLOCK_YELLOW_FLOWER:
        case BLOCK_RED_FLOWER:
        case BLOCK_TORCH:
            return 0.0f;
        case BLOCK_GLOWSTONE:
            return 0.3f;
        case BLOCK_END_STONE:
            return 2.0f;
        case BLOCK_BEDROCK:
            return UNBREAKABLE;
        default:
            return 0.75f;
    }
}

[[nodiscard]] inline constexpr bool unbreakable(BlockId b) { return hardness(b) == UNBREAKABLE; }

// Minimum material tier required so the block yields its drop at all.
[[nodiscard]] inline constexpr MaterialTier required_tier(BlockId b) {
    switch (b) {
        case BLOCK_IRON_ORE: return MaterialTier::Stone;
        case BLOCK_GOLD_ORE:
        case BLOCK_DIAMOND_ORE:
        case BLOCK_OBSIDIAN: return MaterialTier::Iron;
        case BLOCK_STONE:
        case BLOCK_COBBLESTONE:
        case BLOCK_COAL_ORE:
        case BLOCK_GRANITE:
        case BLOCK_DIORITE:
        case BLOCK_ANDESITE:
        case BLOCK_SANDSTONE:
        case BLOCK_BRICKS:
            return MaterialTier::Wood; // needs any pickaxe
        default: return MaterialTier::Hand;
    }
}

// Which tool class accelerates breaking this block.
[[nodiscard]] inline constexpr ToolClass effective_class(BlockId b) {
    switch (b) {
        case BLOCK_STONE:
        case BLOCK_COBBLESTONE:
        case BLOCK_COAL_ORE:
        case BLOCK_IRON_ORE:
        case BLOCK_GOLD_ORE:
        case BLOCK_DIAMOND_ORE:
        case BLOCK_GRANITE:
        case BLOCK_DIORITE:
        case BLOCK_ANDESITE:
        case BLOCK_SANDSTONE:
        case BLOCK_BRICKS:
        case BLOCK_NETHERRACK:
        case BLOCK_OBSIDIAN:
            return ToolClass::Pickaxe;
        case BLOCK_OAK_PLANKS:
        case BLOCK_OAK_LOG:
        case BLOCK_SPRUCE_LOG:
        case BLOCK_BIRCH_LOG:
        case BLOCK_ACACIA_LOG:
        case BLOCK_CRAFTING_TABLE:
            return ToolClass::Axe;
        case BLOCK_FURNACE:
            return ToolClass::Pickaxe;
        case BLOCK_DIRT:
        case BLOCK_GRASS:
        case BLOCK_SAND:
        case BLOCK_GRAVEL:
        case BLOCK_CLAY:
        case BLOCK_SNOW:
            return ToolClass::Shovel;
        default: return ToolClass::None;
    }
}

// Tool properties for a held item id. Block-id-as-item values map by 1:1
// convention only for ids <= ITEM_OAK_LOG; tools live in the item registry
// range and are enumerated explicitly here.
[[nodiscard]] inline constexpr ToolInfo tool_for(ItemId held) {
    switch (held) {
        case ITEM_WOODEN_PICKAXE: return {ToolClass::Pickaxe, MaterialTier::Wood, 2.0f};
        case ITEM_STONE_PICKAXE: return {ToolClass::Pickaxe, MaterialTier::Stone, 4.0f};
        case ITEM_IRON_PICKAXE: return {ToolClass::Pickaxe, MaterialTier::Iron, 6.0f};
        case ITEM_DIAMOND_PICKAXE: return {ToolClass::Pickaxe, MaterialTier::Diamond, 8.0f};
        default: return {};
    }
}

// Seconds required to break `b` while holding `held`. Returns UNBREAKABLE if
// the block cannot be broken, 0.0 for instant-break blocks.
[[nodiscard]] inline constexpr float break_time_seconds(BlockId b, ItemId held) {
    float h = hardness(b);
    if (h == UNBREAKABLE) return UNBREAKABLE;
    if (h <= 0.0f) return 0.0f;

    ToolInfo tool = tool_for(held);
    if (tool.tool_class == ToolClass::None) {
        // Bare hands mine at natural pace (drops may still be tier-gated).
        return h;
    }
    bool effective = tool.tool_class == effective_class(b);
    const float speed = effective ? tool.speed_multiplier : 1.0f;

    // Simplified Minecraft curve: matching tool speeds things up, a held
    // wrong-class tool is worse than fists.
    return h * (effective ? 1.0f : 3.33f) / speed;
}

// ItemStack overload: applies the Efficiency enchantment to mining speed.
[[nodiscard]] inline float break_time_seconds(BlockId b, const ItemStack& held) {
    float t = break_time_seconds(b, held.item);
    uint8_t eff = held.enchant_level_of(EnchantType::Efficiency);
    if (t > 0.0f && t != UNBREAKABLE && eff > 0) {
        t /= (1.0f + 0.45f * static_cast<float>(eff));
    }
    return t;
}

// Unbreaking: with level L, a wear point is skipped with chance L/(L+1).
// `roll01` is the caller's RNG sample in [0,1).
[[nodiscard]] inline bool unbreaking_blocks(uint8_t level, float roll01) {
    return level > 0 && roll01 < static_cast<float>(level) / (static_cast<float>(level) + 1.0f);
}

// Seconds of attack cooldown full swing duration for the held weapon.
[[nodiscard]] inline constexpr float weapon_attack_cooldown(ItemId held) {
    switch (held) {
        case ITEM_WOODEN_SWORD: return 1.6f;
        case ITEM_STONE_SWORD: return 1.6f;
        case ITEM_IRON_SWORD: return 1.6f;
        case ITEM_DIAMOND_SWORD: return 1.6f;
        case ITEM_WOODEN_PICKAXE:
        case ITEM_STONE_PICKAXE:
        case ITEM_IRON_PICKAXE:
        case ITEM_DIAMOND_PICKAXE: return 1.7f;
        default: return 1.25f; // fist
    }
}

// Attack damage dealt by held item (hearts unit where player HP 20).
[[nodiscard]] inline constexpr float weapon_damage(ItemId held) {
    switch (held) {
        case ITEM_WOODEN_SWORD: return 4.0f;
        case ITEM_STONE_SWORD: return 5.0f;
        case ITEM_IRON_SWORD: return 6.0f;
        case ITEM_DIAMOND_SWORD: return 7.0f;
        case ITEM_WOODEN_PICKAXE: return 2.0f;
        case ITEM_STONE_PICKAXE: return 3.0f;
        case ITEM_IRON_PICKAXE: return 4.0f;
        case ITEM_DIAMOND_PICKAXE: return 5.0f;
        default: return 1.0f; // fist
    }
}

// ---- Tool durability (ItemStack.damage vs ItemProperties.max_damage) ----

// Blocks with hardness wear tools down; foliage/instabreak does not.
[[nodiscard]] inline constexpr bool wear_applies(BlockId b) {
    float h = hardness(b);
    return h != UNBREAKABLE && h > 0.0f;
}

// Applies `amount` wear to the held stack. Returns true when the tool broke
// this use (stack is then consumed). Plain items (max_damage == 0) and
// already-worn-out stacks are no-ops.
inline bool damage_tool(ItemStack& s, uint16_t amount) {
    if (s.is_empty() || amount == 0) return false;
    const ItemProperties& props = ItemRegistry::get(s.item);
    if (props.max_damage == 0) return false;
    if (s.damage >= props.max_damage) return false;

    s.damage = static_cast<uint16_t>(s.damage + amount);
    if (s.damage >= props.max_damage) {
        s = ItemStack();
        return true;
    }
    return false;
}

[[nodiscard]] inline constexpr bool is_sword(ItemId id) {
    return id == ITEM_WOODEN_SWORD || id == ITEM_STONE_SWORD ||
           id == ITEM_IRON_SWORD || id == ITEM_DIAMOND_SWORD;
}
[[nodiscard]] inline constexpr bool is_pickaxe(ItemId id) {
    return id == ITEM_WOODEN_PICKAXE || id == ITEM_STONE_PICKAXE ||
           id == ITEM_IRON_PICKAXE || id == ITEM_DIAMOND_PICKAXE;
}

// Durability lost per landed melee hit (vanilla-style: weapons 1, tools 2).
[[nodiscard]] inline constexpr uint16_t attack_durability_cost(ItemId held) {
    if (is_sword(held)) return 1;
    if (is_pickaxe(held)) return 2;
    return 0; // fists are free
}

// ---- Player bow ballistics ----

// Below this hold time (seconds) the release fizzles without firing.
inline constexpr float BOW_MIN_CHARGE = 0.25f;

[[nodiscard]] inline constexpr float bow_charge_capped(float charge) {
    return charge < 1.0f ? charge : 1.0f;
}
// Launch speed in blocks/tick (0.05 s); full draw = 2.0.
[[nodiscard]] inline constexpr float bow_speed(float charge) {
    return 0.5f + 1.5f * bow_charge_capped(charge);
}
// Impact damage; fist baseline 1 at zero charge, 9 at full draw.
[[nodiscard]] inline constexpr float bow_damage(float charge) {
    return 1.0f + 8.0f * bow_charge_capped(charge);
}

// Per-tick gravity applied to projectiles in Game::tick_projectiles.
inline constexpr float ARROW_GRAVITY = 0.045f;

// Vertical drop (blocks) over a straight-line distance `dist` at launch
// `speed` (blocks/tick): 0.5 * g * t^2 with t = dist/speed. Used by aiming.
[[nodiscard]] inline constexpr float arrow_drop(float dist, float speed) {
    float t = (speed > 0.0001f) ? dist / speed : 0.0f;
    return 0.5f * ARROW_GRAVITY * t * t;
}

[[nodiscard]] inline constexpr bool is_bow(ItemId id) { return id == ITEM_BOW; }
[[nodiscard]] inline constexpr bool is_arrow(ItemId id) { return id == ITEM_ARROW; }

struct FoodValue {
    int nutrition;      // hunger points restored (of 20)
    float saturation;   // saturation restored
};
[[nodiscard]] inline constexpr std::optional<FoodValue> food_value(ItemId held) {
    switch (held) {
        case ITEM_APPLE: return FoodValue{4, 2.4f};
        case ITEM_RAW_MEAT: return FoodValue{3, 1.8f};
        case ITEM_COOKED_MEAT: return FoodValue{8, 12.8f};
        default: return std::nullopt;
    }
}

// A single resulting drop stack when a block is mined with a sufficient tool.
struct Drop {
    ItemId item;
    uint8_t count_min;
    uint8_t count_max;
};

// Drops yielded when `broken` is destroyed holding `held`.
// Empty result = nothing drops (wrong tier or self-drops like glass/water).
[[nodiscard]] inline constexpr std::optional<Drop> drop_for(BlockId broken, ItemId held) {
    // Tier gate first: insufficient tool class/tier destroys but drops nothing.
    {
        ToolInfo tool = tool_for(held);
        MaterialTier need = required_tier(broken);
        MaterialTier have = (tool.tool_class != ToolClass::None &&
                             effective_class(broken) == tool.tool_class)
                                ? tool.tier
                                : MaterialTier::Hand;
        if (have < need) return std::nullopt;
    }

    switch (broken) {
        case BLOCK_STONE:
        case BLOCK_GRANITE:
        case BLOCK_DIORITE:
        case BLOCK_ANDESITE:
        case BLOCK_SANDSTONE:
            return Drop{ITEM_COBBLESTONE, 1, 1};
        case BLOCK_COAL_ORE: return Drop{ITEM_COAL, 1, 1};
        case BLOCK_IRON_ORE: return Drop{BLOCK_IRON_ORE, 1, 1};  // smelt later
        case BLOCK_GOLD_ORE: return Drop{BLOCK_GOLD_ORE, 1, 1};
        case BLOCK_DIAMOND_ORE: return Drop{ITEM_DIAMOND, 1, 1};
        case BLOCK_GRASS: return Drop{ITEM_DIRT, 1, 1};
        case BLOCK_OAK_LEAVES: return Drop{ITEM_APPLE, 0, 1}; // chance-resolved below
        case BLOCK_SPRUCE_LEAVES:
        case BLOCK_BIRCH_LEAVES:
        case BLOCK_ACACIA_LEAVES: return Drop{ITEM_STICK, 0, 1};
        case BLOCK_GRAVEL: return Drop{BLOCK_GRAVEL, 1, 1};
        case BLOCK_SNOW: return Drop{BLOCK_SNOW, 1, 1};
        case BLOCK_CRAFTING_TABLE:
        case BLOCK_FURNACE:
        case BLOCK_OAK_PLANKS:
        case BLOCK_OAK_LOG:
        case BLOCK_SPRUCE_LOG:
        case BLOCK_BIRCH_LOG:
        case BLOCK_ACACIA_LOG:
        case BLOCK_SAND:
        case BLOCK_DIRT:
        case BLOCK_CLAY:
        case BLOCK_TERRACOTTA:
        case BLOCK_NETHERRACK:
        case BLOCK_CACTUS:
        case BLOCK_TORCH:
        case BLOCK_REDSTONE_BLOCK:
        case BLOCK_REDSTONE_WIRE:
        case BLOCK_REDSTONE_TORCH:
        case BLOCK_GLOWSTONE:
        case BLOCK_BRICKS:
        case BLOCK_END_STONE:
            return Drop{broken, 1, 1};
        case BLOCK_TALL_GRASS:
            return Drop{static_cast<ItemId>(BLOCK_TALL_GRASS), 1, 1}; // plant fiber
        case BLOCK_GLASS:
        case BLOCK_QUEST_NPC:
        case BLOCK_YELLOW_FLOWER:
        case BLOCK_RED_FLOWER:
            return std::nullopt; // shatter/quest fixture/decorative
        default:
            return std::nullopt;
    }
}

// Resolve a Drop into a concrete ItemStack using `chance01` in [0,1) for the
// optional-count cases (apple from leaves etc.). Kept separate so tests can
// inject deterministic randomness.
[[nodiscard]] inline ItemStack resolve_drop(Drop d, float chance01) {
    uint8_t count = d.count_max;
    if (d.count_min != d.count_max && chance01 >= 0.15f) count = d.count_min;
    if (count == 0 || d.item == ITEM_AIR) return ItemStack();
    return ItemStack(d.item, count);
}

} // namespace mc::mining

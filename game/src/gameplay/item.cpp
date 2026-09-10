#include "gameplay/item.hpp"
#include "world/block.hpp"

#include <array>
#include <cctype>

namespace mc {

namespace {
// We use a small array covering up to ITEM_COUNT.
// Empty slots (between blocks and items) default to Air.
constexpr std::array<ItemProperties, ITEM_COUNT> build_table() {
    std::array<ItemProperties, ITEM_COUNT> table{};
    for (int i = 0; i < ITEM_COUNT; ++i) {
        table[i] = {"unknown", 64, 0};
    }
    
    table[ITEM_AIR] = {"air", 0, 0};
    table[ITEM_STONE] = {"stone", 64, 0};
    table[ITEM_GRASS_BLOCK] = {"grass_block", 64, 0};
    table[ITEM_DIRT] = {"dirt", 64, 0};
    table[ITEM_COBBLESTONE] = {"cobblestone", 64, 0};
    table[ITEM_OAK_PLANKS] = {"oak_planks", 64, 0};
    table[ITEM_OAK_LOG] = {"oak_log", 64, 0};
    
    table[ITEM_IRON_INGOT] = {"iron_ingot", 64, 0};
    table[ITEM_GOLD_INGOT] = {"gold_ingot", 64, 0};
    table[ITEM_DIAMOND] = {"diamond", 64, 0};
    table[ITEM_STICK] = {"stick", 64, 0};
    
    table[ITEM_WOODEN_PICKAXE] = {"wooden_pickaxe", 1, 59};
    table[ITEM_STONE_PICKAXE] = {"stone_pickaxe", 1, 131};
    table[ITEM_IRON_PICKAXE] = {"iron_pickaxe", 1, 250};
    table[ITEM_DIAMOND_PICKAXE] = {"diamond_pickaxe", 1, 1561};

    table[ITEM_COAL] = {"coal", 64, 0};
    table[ITEM_APPLE] = {"apple", 64, 0};
    table[ITEM_RAW_MEAT] = {"raw_meat", 64, 0};
    table[ITEM_COOKED_MEAT] = {"cooked_meat", 64, 0};
    table[ITEM_CHARCOAL] = {"charcoal", 64, 0};
    table[ITEM_BOW] = {"bow", 1, 384};
    table[ITEM_ARROW] = {"arrow", 64, 0};
    table[ITEM_BOOK] = {"book", 64, 0};
    table[ITEM_RAW_COPPER] = {"raw_copper", 64, 0};
    table[ITEM_REDSTONE] = {"redstone", 64, 0};
    table[ITEM_LAPIS] = {"lapis", 64, 0};
    table[ITEM_WOODEN_SWORD] = {"wooden_sword", 1, 59};
    table[ITEM_STONE_SWORD] = {"stone_sword", 1, 131};
    table[ITEM_IRON_SWORD] = {"iron_sword", 1, 250};
    table[ITEM_DIAMOND_SWORD] = {"diamond_sword", 1, 1561};

    return table;
}

constexpr auto TABLE = build_table();
} // namespace

const ItemProperties& ItemRegistry::get(ItemId id) {
    if (id >= ITEM_COUNT) return TABLE[ITEM_AIR];
    return TABLE[id];
}

ItemId ItemRegistry::id_from_name(std::string_view name_in) {
    std::string n;
    n.reserve(name_in.size());
    for (char c : name_in) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Blocks first (their ids double as item ids), then pure items >= 256.
    for (BlockId b = 1; b < BLOCK_COUNT; ++b) {
        if (BLOCK_PROPERTIES_TABLE[b].name == n) return static_cast<ItemId>(b);
    }
    for (ItemId id = 1; id < ITEM_COUNT; ++id) {
        if (TABLE[id].name == n) return id;
    }
    return ITEM_AIR;
}

} // namespace mc

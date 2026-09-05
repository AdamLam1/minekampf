#pragma once

#include <cstdint>
#include <string_view>

#include "core/config.hpp"
#include "core/types.hpp"
#include <array>

namespace mc {

// Block state identifier. Air is always 0 (PHASE2 §1.1).
// 16-bit is sufficient for < 65k block states — Golden Rule keeps it small.
using BlockId = uint16_t;

// Well-known block ids. Stable numeric ordering (air=0 is required).
inline constexpr BlockId BLOCK_AIR = 0;
inline constexpr BlockId BLOCK_STONE = 1;
inline constexpr BlockId BLOCK_GRASS = 2;
inline constexpr BlockId BLOCK_DIRT = 3;
inline constexpr BlockId BLOCK_COBBLESTONE = 4;
inline constexpr BlockId BLOCK_OAK_PLANKS = 5;
inline constexpr BlockId BLOCK_OAK_LOG = 6;
inline constexpr BlockId BLOCK_OAK_LEAVES = 7;
inline constexpr BlockId BLOCK_SAND = 8;
inline constexpr BlockId BLOCK_GRAVEL = 9;
inline constexpr BlockId BLOCK_SANDSTONE = 10;
inline constexpr BlockId BLOCK_WATER = 11;
inline constexpr BlockId BLOCK_LAVA = 12;
inline constexpr BlockId BLOCK_BEDROCK = 13;
inline constexpr BlockId BLOCK_GLOWSTONE = 14;
inline constexpr BlockId BLOCK_GLASS = 15;
inline constexpr BlockId BLOCK_SNOW = 16;
inline constexpr BlockId BLOCK_ICE = 17;
inline constexpr BlockId BLOCK_CLAY = 18;
inline constexpr BlockId BLOCK_TERRACOTTA = 19;
inline constexpr BlockId BLOCK_COAL_ORE = 20;
inline constexpr BlockId BLOCK_IRON_ORE = 21;
inline constexpr BlockId BLOCK_GOLD_ORE = 22;
inline constexpr BlockId BLOCK_DIAMOND_ORE = 23;
inline constexpr BlockId BLOCK_GRANITE = 24;
inline constexpr BlockId BLOCK_DIORITE = 25;
inline constexpr BlockId BLOCK_ANDESITE = 26;
inline constexpr BlockId BLOCK_BRICKS = 27;
inline constexpr BlockId BLOCK_NETHERRACK = 28;
inline constexpr BlockId BLOCK_OBSIDIAN = 29;
inline constexpr BlockId BLOCK_SPRUCE_LOG = 30;
inline constexpr BlockId BLOCK_SPRUCE_LEAVES = 31;
inline constexpr BlockId BLOCK_BIRCH_LOG = 32;
inline constexpr BlockId BLOCK_BIRCH_LEAVES = 33;
inline constexpr BlockId BLOCK_TALL_GRASS = 34;
inline constexpr BlockId BLOCK_YELLOW_FLOWER = 35;
inline constexpr BlockId BLOCK_RED_FLOWER = 36;
inline constexpr BlockId BLOCK_NETHER_PORTAL = 37;
inline constexpr BlockId BLOCK_END_PORTAL_FRAME = 38;
inline constexpr BlockId BLOCK_END_PORTAL = 39;
inline constexpr BlockId BLOCK_END_STONE = 40;

// Redstone blocks (must come after fluid flows to avoid ID collision)
inline constexpr BlockId BLOCK_REDSTONE_WIRE = 51;inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_1 = 52;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_2 = 53;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_3 = 54;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_4 = 55;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_5 = 56;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_6 = 57;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_7 = 58;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_8 = 59;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_9 = 60;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_10 = 61;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_11 = 62;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_12 = 63;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_13 = 64;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_14 = 65;
inline constexpr BlockId BLOCK_REDSTONE_WIRE_POWER_15 = 66;
inline constexpr BlockId BLOCK_REDSTONE_TORCH = 67;
inline constexpr BlockId BLOCK_REDSTONE_BLOCK = 68;

// Flowing fluid blocks (levels 1-7: 1 = almost source, 7 = thinnest)
inline constexpr BlockId BLOCK_WATER_FLOW_1 = 41;
inline constexpr BlockId BLOCK_WATER_FLOW_2 = 42;
inline constexpr BlockId BLOCK_WATER_FLOW_3 = 43;
inline constexpr BlockId BLOCK_WATER_FLOW_4 = 44;
inline constexpr BlockId BLOCK_WATER_FLOW_5 = 45;
inline constexpr BlockId BLOCK_WATER_FLOW_6 = 46;
inline constexpr BlockId BLOCK_WATER_FLOW_7 = 47;
inline constexpr BlockId BLOCK_LAVA_FLOW_1 = 48;
inline constexpr BlockId BLOCK_LAVA_FLOW_2 = 49;
inline constexpr BlockId BLOCK_LAVA_FLOW_3 = 50;

inline constexpr BlockId BLOCK_OAK_WOOD = 5; // Alias for Oak Planks

// Production blocks (appended after the legacy id ranges).
inline constexpr BlockId BLOCK_TORCH = 69;
inline constexpr BlockId BLOCK_CACTUS = 70;
inline constexpr BlockId BLOCK_CRAFTING_TABLE = 71;
inline constexpr BlockId BLOCK_FURNACE = 72;
inline constexpr BlockId BLOCK_QUEST_NPC = 73;
inline constexpr BlockId BLOCK_ENCHANTING_TABLE = 74;
inline constexpr BlockId BLOCK_BOOKSHELF = 75;
// Redstone components (ON/OFF as separate state ids, matching the wire pattern)
inline constexpr BlockId BLOCK_LEVER_OFF = 76;
inline constexpr BlockId BLOCK_LEVER_ON = 77;
inline constexpr BlockId BLOCK_PRESSURE_PLATE_OFF = 78;
inline constexpr BlockId BLOCK_PRESSURE_PLATE_ON = 79;
inline constexpr BlockId BLOCK_REDSTONE_LAMP_OFF = 80;
inline constexpr BlockId BLOCK_REDSTONE_LAMP_ON = 81;
inline constexpr BlockId BLOCK_REPEATER_OFF = 82;
inline constexpr BlockId BLOCK_REPEATER_ON = 83;
inline constexpr BlockId BLOCK_ACACIA_LOG = 84;
inline constexpr BlockId BLOCK_ACACIA_LEAVES = 85;

inline constexpr BlockId BLOCK_COUNT = 86;

[[nodiscard]] inline constexpr bool is_lever(BlockId b) {
    return b == BLOCK_LEVER_OFF || b == BLOCK_LEVER_ON;
}
[[nodiscard]] inline constexpr bool is_pressure_plate(BlockId b) {
    return b == BLOCK_PRESSURE_PLATE_OFF || b == BLOCK_PRESSURE_PLATE_ON;
}
[[nodiscard]] inline constexpr bool is_redstone_lamp(BlockId b) {
    return b == BLOCK_REDSTONE_LAMP_OFF || b == BLOCK_REDSTONE_LAMP_ON;
}
[[nodiscard]] inline constexpr bool is_repeater(BlockId b) {
    return b == BLOCK_REPEATER_OFF || b == BLOCK_REPEATER_ON;
}

// Fluid helpers
[[nodiscard]] inline constexpr bool is_water(BlockId b) {
    return b == BLOCK_WATER || (b >= BLOCK_WATER_FLOW_1 && b <= BLOCK_WATER_FLOW_7);
}
[[nodiscard]] inline constexpr bool is_lava(BlockId b) {
    return b == BLOCK_LAVA || (b >= BLOCK_LAVA_FLOW_1 && b <= BLOCK_LAVA_FLOW_3);
}
[[nodiscard]] inline constexpr bool is_fluid(BlockId b) { return is_water(b) || is_lava(b); }
[[nodiscard]] inline constexpr int fluid_level(BlockId b) {
    if (b == BLOCK_WATER || b == BLOCK_LAVA) return 0;
    if (b >= BLOCK_WATER_FLOW_1 && b <= BLOCK_WATER_FLOW_7) return static_cast<int>(b - BLOCK_WATER_FLOW_1 + 1);
    if (b >= BLOCK_LAVA_FLOW_1 && b <= BLOCK_LAVA_FLOW_3) return static_cast<int>(b - BLOCK_LAVA_FLOW_1 + 1);
    return -1;
}
[[nodiscard]] inline constexpr BlockId water_for_level(int level) {
    if (level <= 0) return BLOCK_WATER;
    if (level >= 7) return BLOCK_WATER_FLOW_7;
    return static_cast<BlockId>(BLOCK_WATER_FLOW_1 + level - 1);
}
[[nodiscard]] inline constexpr bool is_fluid_source(BlockId b) { return b == BLOCK_WATER || b == BLOCK_LAVA; }

// Atlas tile indices (must match the procedurally-generated atlas in renderer).
enum class Tile : uint16_t {
    Air = 0,
    Stone = 1,
    GrassTop = 2,
    GrassSide = 3,
    Dirt = 4,
    Cobblestone = 5,
    Planks = 6,
    LogTop = 7,
    LogSide = 8,
    Leaves = 9,
    Sand = 10,
    Gravel = 11,
    Sandstone = 12,
    SandstoneBottom = 13,
    Water = 14,
    Lava = 15,
    Bedrock = 16,
    Glowstone = 17,
    Glass = 18,
    Snow = 19,
    Ice = 20,
    Clay = 21,
    Terracotta = 22,
    CoalOre = 23,
    IronOre = 24,
    GoldOre = 25,
    DiamondOre = 26,
    Granite = 27,
    Diorite = 28,
    Andesite = 29,
    Bricks = 30,
    Netherrack = 31,
    Obsidian = 32,
    NetherPortal = 33,
    EndPortalFrame = 34,
    EndPortal = 35,
    EndStone = 36,
    Hand = 37,
    TallGrass = 38,
    FlowerYellow = 39,
    FlowerRed = 40,
    Torch = 41,
    Cactus = 42,
    Bookshelf = 43,
    EnchantingTableTop = 44,
    EnchantingTableSide = 45,
    Lever = 46,
    PressurePlate = 47,
    LampOff = 48,
    LampOn = 49,
    Repeater = 50,
    RedstoneWireOff = 51,
    RedstoneWireOn = 52,
    RedstoneTorch = 53,
    RedstoneBlock = 54,
    CraftingTableTop = 55,
    CraftingTableSide = 56,
    FurnaceFront = 57,
    FurnaceFrontLit = 58,
    QuestNpc = 59,
    DestroyStage0 = 60, // mining crack overlay, stage grows with progress
    DestroyStage1 = 61,
    DestroyStage2 = 62,
    DestroyStage3 = 63,
    DestroyStage4 = 64,
    DestroyStage5 = 65,
    DestroyStage6 = 66,
    DestroyStage7 = 67,
    DestroyStage8 = 68,
    DestroyStage9 = 69,
    AcaciaLogSide = 70,
    AcaciaLogTop = 71,
    AcaciaLeaves = 72,
    Count = 73,
};

// Stable tile names (texture overrides in assets/textures/<name>.png, debug
// tooling). Order must match the enum above.
inline constexpr std::array<std::string_view, static_cast<size_t>(Tile::Count)> TILE_NAMES = {
    "Air", "Stone", "Dirt", "GrassTop", "GrassSide", "Cobblestone", "Planks",
    "LogTop", "LogSide", "Leaves", "Sand", "Gravel", "Sandstone",
    "SandstoneBottom", "Water", "Lava", "Bedrock", "Glowstone", "Glass", "Snow",
    "Ice", "Clay", "Terracotta", "CoalOre", "IronOre", "GoldOre", "DiamondOre",
    "Granite", "Diorite", "Andesite", "Bricks", "Netherrack", "Obsidian",
    "NetherPortal", "EndPortalFrame", "EndPortal", "EndStone", "Hand",
    "TallGrass", "FlowerYellow", "FlowerRed", "Torch", "Cactus", "Bookshelf",
    "EnchantingTableTop", "EnchantingTableSide", "Lever", "PressurePlate",
    "LampOff", "LampOn", "Repeater", "RedstoneWireOff", "RedstoneWireOn",
    "RedstoneTorch", "RedstoneBlock", "CraftingTableTop", "CraftingTableSide",
    "FurnaceFront", "FurnaceFrontLit", "QuestNpc", "DestroyStage0",
    "DestroyStage1", "DestroyStage2", "DestroyStage3", "DestroyStage4",
    "DestroyStage5", "DestroyStage6", "DestroyStage7", "DestroyStage8",
    "DestroyStage9", "AcaciaLogSide", "AcaciaLogTop", "AcaciaLeaves",
};

// Static properties of a block type. Looked up by id from a fixed table.
struct BlockProperties {
    std::string_view name;
    bool solid;          // has collision (PHASE4 §1.2)
    bool opaque;         // fully blocks light/sight (PHASE3 §1.2)
    bool transparent;    // rendered with alpha (water, glass, leaves-fancy)
    bool liquid;         // water/lava — no collision, swim physics
    bool full_cube;      // occupies entire 1x1x1 (for face culling)
    uint8_t light_emission; // 0-15 (PHASE3 §4.1)
    uint8_t opacity;     // light attenuation (1 for most solids, 0 for air/glass)
    float slipperiness;  // 0.6 default, 0.98 ice (PHASE4 §3.1)
    Tile tile_top;
    Tile tile_bottom;
    Tile tile_side;
};

// Fixed properties table. Indexed by BlockId. Order matches block.hpp ids.
// clang-format off
inline constexpr std::array<BlockProperties, BLOCK_COUNT> BLOCK_PROPERTIES_TABLE = {{
    {"air",          false, false, true,  false, false, 0,  0, 0.6f,  Tile::Air,             Tile::Air,             Tile::Air},
    {"stone",        true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Stone,           Tile::Stone,           Tile::Stone},
    {"grass_block",  true,  true,  false, false, true,  0,  1, 0.6f,  Tile::GrassTop,        Tile::Dirt,            Tile::GrassSide},
    {"dirt",         true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Dirt,            Tile::Dirt,            Tile::Dirt},
    {"cobblestone",  true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Cobblestone,     Tile::Cobblestone,     Tile::Cobblestone},
    {"oak_planks",   true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Planks,          Tile::Planks,          Tile::Planks},
    {"oak_log",      true,  true,  false, false, true,  0,  1, 0.6f,  Tile::LogTop,          Tile::LogTop,          Tile::LogSide},
    {"oak_leaves",   true,  false, true,  false, false, 0,  1, 0.6f,  Tile::Leaves,          Tile::Leaves,          Tile::Leaves},
    {"sand",         true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Sand,            Tile::Sand,            Tile::Sand},
    {"gravel",       true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Gravel,          Tile::Gravel,          Tile::Gravel},
    {"sandstone",    true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Sandstone,       Tile::SandstoneBottom, Tile::Sandstone},
    {"water",        false, false, true,  true,  false, 0,  0, 0.6f,  Tile::Water,           Tile::Water,           Tile::Water},
    {"lava",         false, false, false, true,  false, 15, 0, 0.6f,  Tile::Lava,            Tile::Lava,            Tile::Lava},
    {"bedrock",      true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Bedrock,         Tile::Bedrock,         Tile::Bedrock},
    {"glowstone",    true,  true,  false, false, true,  15, 1, 0.6f,  Tile::Glowstone,       Tile::Glowstone,       Tile::Glowstone},
    {"glass",        true,  false, true,  false, true,  0,  0, 0.6f,  Tile::Glass,           Tile::Glass,           Tile::Glass},
    {"snow",         true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Snow,            Tile::Snow,            Tile::Snow},
    {"ice",          true,  true,  true,  false, true,  0,  0, 0.98f, Tile::Ice,             Tile::Ice,             Tile::Ice},
    {"clay",         true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Clay,            Tile::Clay,            Tile::Clay},
    {"terracotta",   true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Terracotta,      Tile::Terracotta,      Tile::Terracotta},
    {"coal_ore",     true,  true,  false, false, true,  0,  1, 0.6f,  Tile::CoalOre,         Tile::CoalOre,         Tile::CoalOre},
    {"iron_ore",     true,  true,  false, false, true,  0,  1, 0.6f,  Tile::IronOre,         Tile::IronOre,         Tile::IronOre},
    {"gold_ore",     true,  true,  false, false, true,  0,  1, 0.6f,  Tile::GoldOre,         Tile::GoldOre,         Tile::GoldOre},
    {"diamond_ore",  true,  true,  false, false, true,  0,  1, 0.6f,  Tile::DiamondOre,      Tile::DiamondOre,      Tile::DiamondOre},
    {"granite",      true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Granite,         Tile::Granite,         Tile::Granite},
    {"diorite",      true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Diorite,         Tile::Diorite,         Tile::Diorite},
    {"andesite",     true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Andesite,        Tile::Andesite,        Tile::Andesite},
    {"bricks",       true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Bricks,          Tile::Bricks,          Tile::Bricks},
    {"netherrack",   true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Netherrack,      Tile::Netherrack,      Tile::Netherrack},
    {"obsidian",     true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Obsidian,        Tile::Obsidian,        Tile::Obsidian},
    {"spruce_log",   true,  true,  false, false, true,  0,  1, 0.6f,  Tile::LogTop,          Tile::LogTop,          Tile::LogSide},
    {"spruce_leaves",true,  false, true,  false, false, 0,  1, 0.6f,  Tile::Leaves,          Tile::Leaves,          Tile::Leaves},
    {"acacia_log",   true,  true,  false, false, true,  0,  1, 0.6f,  Tile::AcaciaLogTop,    Tile::AcaciaLogTop,    Tile::AcaciaLogSide},
    {"acacia_leaves",true,  false, true,  false, false, 0,  1, 0.6f,  Tile::AcaciaLeaves,    Tile::AcaciaLeaves,    Tile::AcaciaLeaves},
    {"birch_log",    true,  true,  false, false, true,  0,  1, 0.6f,  Tile::LogTop,          Tile::LogTop,          Tile::LogSide},
    {"birch_leaves", true,  false, true,  false, false, 0,  1, 0.6f,  Tile::Leaves,          Tile::Leaves,          Tile::Leaves},
    {"tall_grass",   false, false, true,  false, false, 0,  0, 0.6f,  Tile::TallGrass,       Tile::TallGrass,       Tile::TallGrass},
    {"yellow_flower",false, false, true,  false, false, 0,  0, 0.6f,  Tile::FlowerYellow,    Tile::FlowerYellow,    Tile::FlowerYellow},
    {"red_flower",   false, false, true,  false, false, 0,  0, 0.6f,  Tile::FlowerRed,       Tile::FlowerRed,       Tile::FlowerRed},
    {"nether_portal",false, false, true,  false, false, 11, 0, 0.6f,  Tile::NetherPortal,    Tile::NetherPortal,    Tile::NetherPortal},
    {"end_portal_frame",true,true, false, false, true,  0,  1, 0.6f,  Tile::EndPortalFrame,  Tile::EndStone,        Tile::EndPortalFrame},
    {"end_portal",   false, false, true,  false, false, 15, 0, 0.6f,  Tile::EndPortal,       Tile::EndPortal,       Tile::EndPortal},
    {"end_stone",    true,  true,  false, false, true,  0,  1, 0.6f,  Tile::EndStone,        Tile::EndStone,        Tile::EndStone},
    {"water_flow_1", false, false, true,  true,  false, 0,  0, 0.6f,  Tile::Water,           Tile::Water,           Tile::Water},
    {"water_flow_2", false, false, true,  true,  false, 0,  0, 0.6f,  Tile::Water,           Tile::Water,           Tile::Water},
    {"water_flow_3", false, false, true,  true,  false, 0,  0, 0.6f,  Tile::Water,           Tile::Water,           Tile::Water},
    {"water_flow_4", false, false, true,  true,  false, 0,  0, 0.6f,  Tile::Water,           Tile::Water,           Tile::Water},
    {"water_flow_5", false, false, true,  true,  false, 0,  0, 0.6f,  Tile::Water,           Tile::Water,           Tile::Water},
    {"water_flow_6", false, false, true,  true,  false, 0,  0, 0.6f,  Tile::Water,           Tile::Water,           Tile::Water},
    {"water_flow_7", false, false, true,  true,  false, 0,  0, 0.6f,  Tile::Water,           Tile::Water,           Tile::Water},
    {"lava_flow_1",  false, false, false, true,  false, 15, 0, 0.6f,  Tile::Lava,            Tile::Lava,            Tile::Lava},
    {"lava_flow_2",  false, false, false, true,  false, 15, 0, 0.6f,  Tile::Lava,            Tile::Lava,            Tile::Lava},
    {"lava_flow_3",  false, false, false, true,  false, 15, 0, 0.6f,  Tile::Lava,            Tile::Lava,            Tile::Lava},
    // Redstone blocks (wire power level drives emissive on-tile vs off-tile)
    {"redstone_wire", false, false, false, false, false, 0,  0, 0.6f,  Tile::RedstoneWireOff, Tile::RedstoneWireOff, Tile::RedstoneWireOff},
    {"redstone_wire_p1",false,false, false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p2",false,false, false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p3",false,false, false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p4",false,false, false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p5",false,false, false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p6",false,false, false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p7",false,false, false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p8",false,false, false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p9",false,false, false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p10",false,false,false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p11",false,false,false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p12",false,false,false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p13",false,false,false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p14",false,false,false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_wire_p15",false,false,false, false, false, 7,  0, 0.6f,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn,  Tile::RedstoneWireOn},
    {"redstone_torch", true,  false, false, false, false, 7,  0, 0.6f,  Tile::RedstoneTorch,   Tile::RedstoneTorch,   Tile::RedstoneTorch},
    {"redstone_block", true,  true,  false, false, true,  0,  1, 0.6f,  Tile::RedstoneBlock,   Tile::RedstoneBlock,   Tile::RedstoneBlock},
    {"torch",          false, false, true,  false, false, 14, 0, 0.6f,  Tile::Torch,           Tile::Torch,           Tile::Torch},
    {"cactus",         true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Cactus,          Tile::Cactus,          Tile::Cactus},
    {"crafting_table", true,  true,  false, false, true,  0,  1, 0.6f,  Tile::CraftingTableTop,Tile::Planks,          Tile::CraftingTableSide},
    {"furnace",        true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Stone,           Tile::Stone,           Tile::FurnaceFront},
    {"quest_npc",      true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Planks,          Tile::Planks,          Tile::QuestNpc},
    {"enchanting_table",true, true,  false, false, true,  0,  1, 0.6f,  Tile::EnchantingTableTop, Tile::Obsidian,  Tile::EnchantingTableSide},
    {"bookshelf",      true,  true,  false, false, true,  0,  1, 0.6f,  Tile::Bookshelf,       Tile::Planks,          Tile::Bookshelf},
    {"lever_off",      false, false, false, false, false, 0,  0, 0.6f,  Tile::Lever,           Tile::Lever,           Tile::Lever},
    {"lever_on",       false, false, false, false, false, 0,  0, 0.6f,  Tile::Lever,           Tile::Lever,           Tile::Lever},
    {"pressure_plate_off",false,false,false, false, false, 0, 0, 0.6f,  Tile::PressurePlate,   Tile::PressurePlate,   Tile::PressurePlate},
    {"pressure_plate_on", false,false,false, false, false, 0, 0, 0.6f,  Tile::PressurePlate,   Tile::PressurePlate,   Tile::PressurePlate},
    {"redstone_lamp_off",true, true,  false, false, true,  0,  1, 0.6f,  Tile::LampOff,         Tile::LampOff,         Tile::LampOff},
    {"redstone_lamp_on", true, true,  false, false, true,  15, 1, 0.6f,  Tile::LampOn,          Tile::LampOn,          Tile::LampOn},
    {"repeater_off",   false, false, false, false, false, 0,  0, 0.6f,  Tile::Repeater,        Tile::Repeater,        Tile::Repeater},
    {"repeater_on",    false, false, false, false, false, 0,  0, 0.6f,  Tile::Repeater,        Tile::Repeater,        Tile::Repeater},
}};
// clang-format on

// Global registry: a fixed array indexed by BlockId.
class BlockRegistry {
public:
    static constexpr const BlockProperties& get(BlockId id) {
        if (id >= BLOCK_COUNT) return BLOCK_PROPERTIES_TABLE[BLOCK_AIR];
        return BLOCK_PROPERTIES_TABLE[id];
    }

    BlockRegistry() = delete;
};

// Convenience predicates used in hot paths.
[[nodiscard]] inline bool is_air(BlockId b) { return b == BLOCK_AIR; }
[[nodiscard]] inline bool is_solid(BlockId b) { return BlockRegistry::get(b).solid; }
[[nodiscard]] inline bool is_opaque(BlockId b) { return BlockRegistry::get(b).opaque; }
[[nodiscard]] inline bool is_transparent(BlockId b) { return BlockRegistry::get(b).transparent; }
[[nodiscard]] inline bool is_liquid(BlockId b) { return BlockRegistry::get(b).liquid || is_fluid(b); }
[[nodiscard]] inline bool is_targetable(BlockId b) { return b != BLOCK_AIR && !is_liquid(b); }
[[nodiscard]] inline bool is_full_cube(BlockId b) { return BlockRegistry::get(b).full_cube; }
[[nodiscard]] inline uint8_t light_emission(BlockId b) { return BlockRegistry::get(b).light_emission; }
[[nodiscard]] inline uint8_t opacity(BlockId b) { return BlockRegistry::get(b).opacity; }
[[nodiscard]] inline float slipperiness(BlockId b) { return BlockRegistry::get(b).slipperiness; }

// Tile for a block on a given face direction.
[[nodiscard]] inline Tile tile_for_face(BlockId b, Direction d) {
    const auto& p = BlockRegistry::get(b);
    if (d == Direction::Up) return p.tile_top;
    if (d == Direction::Down) return p.tile_bottom;
    return p.tile_side;
}

} // namespace mc

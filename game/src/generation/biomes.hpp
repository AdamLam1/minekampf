#pragma once

#include <cstdint>
#include <string_view>

#include "core/types.hpp"
#include "world/block.hpp"

namespace mc {

// A small biome set (PHASE6 §5). Selected from temperature/humidity noise
// via nearest-parameter match. Each biome defines surface composition.
enum class Biome : uint8_t {
    Ocean,
    Beach,
    Plains,
    Forest,
    Desert,
    Savanna,
    Taiga,
    Snowy,
    Mountains,
    Count,
};

struct BiomeInfo {
    std::string_view name;
    float temperature; // 0..2 (cold..hot)
    float humidity;    // 0..1
    float continentalness; // -1..1 (ocean..land)
    float ridges;      // 0..1 (flat..mountainous)
    BlockId surface;   // top block (grass/sand/snow)
    BlockId subsurface; // a few blocks below surface (dirt/sand/sandstone)
    BlockId filler;    // deep ground (stone)
    BlockId underwater; // floor under water (sand/dirt)
    uint8_t min_tree_chance; // 0 = no trees, else ~1/chance per column
};

[[nodiscard]] const BiomeInfo& biome_info(Biome b);

// Select a biome from sampled noise parameters (PHASE6 §5.1).
[[nodiscard]] Biome select_biome(float temperature, float humidity, float continentalness, float ridges);

} // namespace mc

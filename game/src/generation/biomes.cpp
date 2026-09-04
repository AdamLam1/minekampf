#include "generation/biomes.hpp"

#include <array>
#include <cmath>

namespace mc {

namespace {
// clang-format off
constexpr std::array<BiomeInfo, static_cast<size_t>(Biome::Count)> BIOMES = {{
    {"ocean",     0.5f, 0.5f, -0.6f, 0.0f, BLOCK_DIRT,    BLOCK_DIRT,    BLOCK_STONE,    BLOCK_SAND,  0},
    {"beach",     0.7f, 0.4f, -0.1f, 0.0f, BLOCK_SAND,    BLOCK_SAND,    BLOCK_SANDSTONE,BLOCK_SAND, 0},
    {"plains",    0.8f, 0.4f,  0.3f, 0.0f, BLOCK_GRASS,   BLOCK_DIRT,    BLOCK_STONE,    BLOCK_DIRT,  25},
    {"forest",    0.7f, 0.8f,  0.3f, 0.0f, BLOCK_GRASS,   BLOCK_DIRT,    BLOCK_STONE,    BLOCK_DIRT,  8},
    {"desert",    2.0f, 0.0f,  0.4f, 0.0f, BLOCK_SAND,    BLOCK_SAND,    BLOCK_SANDSTONE,BLOCK_SAND, 0},
    {"savanna",   1.2f, 0.1f,  0.3f, 0.0f, BLOCK_GRASS,   BLOCK_DIRT,    BLOCK_STONE,    BLOCK_DIRT, 0},
    {"taiga",     0.25f,0.8f,  0.3f, 0.0f, BLOCK_GRASS,   BLOCK_DIRT,    BLOCK_STONE,    BLOCK_DIRT,  12},
    {"snowy",     0.0f, 0.5f,  0.3f, 0.0f, BLOCK_SNOW,    BLOCK_DIRT,    BLOCK_STONE,    BLOCK_DIRT,  16},
    {"mountains", 0.2f, 0.5f,  0.5f, 0.8f, BLOCK_STONE,   BLOCK_STONE,   BLOCK_STONE,    BLOCK_STONE, 0},
}};
// clang-format on
} // namespace

const BiomeInfo& biome_info(Biome b) {
    size_t i = static_cast<size_t>(b);
    if (i >= BIOMES.size()) return BIOMES[static_cast<size_t>(Biome::Plains)];
    return BIOMES[i];
}

Biome select_biome(float temperature, float humidity, float continentalness, float ridges) {
    // Coarse categorical selection matching PHASE6 §2.2 terrain-from-density.
    if (continentalness < -0.4f) return Biome::Ocean;
    if (continentalness < -0.05f) return Biome::Beach;

    if (ridges > 0.6f) return Biome::Mountains;

    if (temperature < 0.15f) return Biome::Snowy;
    if (temperature < 0.4f) return Biome::Taiga;
    if (temperature > 1.5f) return Biome::Desert;
    if (temperature > 1.0f && humidity < 0.25f) return Biome::Savanna;

    if (humidity > 0.6f) return Biome::Forest;
    return Biome::Plains;
}

} // namespace mc

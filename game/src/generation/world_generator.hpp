#pragma once

#include <cstdint>

#include "core/random.hpp"
#include "core/types.hpp"
#include "generation/biomes.hpp"
#include "generation/noise.hpp"
#include "generation/structure_generator.hpp"
#include "world/chunk.hpp"
#include "world/dimension.hpp"

namespace mc {

// Procedural world generator (PHASE6). Deterministic from a 64-bit seed.
// Pipeline per chunk column: noise → biome → terrain density → caves →
// liquids → ores → features (trees). Runs on worker threads (pure function
// of position + seed — Golden Rule #42).
class WorldGenerator {
public:
    DimensionId dimension_ = DimensionId::Overworld;

    WorldGenerator() = default;
    explicit WorldGenerator(uint64_t seed, DimensionId dim = DimensionId::Overworld) 
        : dimension_(dim),
          seed_(seed),
          continentalness_(seed),
          erosion_(seed + 1),
          ridges_(seed + 2),
          temperature_(seed + 3),
          humidity_(seed + 4),
          cave_(seed + 5),
          ore_(seed + 6),
          tree_(seed + 7),
          structures_(seed) {}

    // Fill `chunk` with terrain + features. Sets status to Full and marks
    // dirty for meshing/light. Does not compute light (separate pass).
    // Const: only reads generator state (noise tables) — safe to call from
    // multiple worker threads concurrently (Golden Rule #42 determinism).
    void generate(Chunk& chunk) const;
    
    // Calculates terrain height and biome
    [[nodiscard]] int terrain_height(int world_x, int world_z, Biome& out_biome) const;

    [[nodiscard]] uint64_t seed() const { return seed_; }

private:
    // Terrain heights + biomes of the chunk's 16x16 columns, computed once in
    // generate_terrain and reused by generate_features (its 20x20 scan only
    // needs to run the full noise stack for the 4-block border ring).
    struct TerrainColumnCache {
        std::array<int, CHUNK_SIZE * CHUNK_SIZE> tops{};
        std::array<Biome, CHUNK_SIZE * CHUNK_SIZE> biomes{};
    };
    void generate_terrain(Chunk& chunk, TerrainColumnCache& cache) const;
    void generate_features(Chunk& chunk, const TerrainColumnCache& cache) const;

    [[nodiscard]] bool is_cave(int world_x, int world_y, int world_z, int terrain_top) const;
    [[nodiscard]] BlockId ore_at(int world_x, int world_y, int world_z, int terrain_top) const;
    void place_tree(Chunk& chunk, int tx, int tz, int surface_y, Biome biome, Rng& rng) const;

    uint64_t seed_;
    PerlinNoise continentalness_;
    PerlinNoise erosion_;
    PerlinNoise ridges_;
    PerlinNoise temperature_;
    PerlinNoise humidity_;
    PerlinNoise cave_;
    PerlinNoise ore_;
    PerlinNoise tree_;
    
    StructureGenerator structures_{0};
};

} // namespace mc

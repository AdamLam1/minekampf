#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include "world/world.hpp"
#include "gameplay/player.hpp"
#include "save/region_file.hpp"

namespace mc {

// Persisted snapshot of one placed furnace (block-entity state).
struct FurnaceSave {
    int32_t dim = 0;
    int32_t x = 0, y = 0, z = 0;
    int32_t in_item = 0, in_count = 0;
    int32_t fuel_item = 0, fuel_count = 0;
    int32_t out_item = 0, out_count = 0;
    int32_t burn_left = 0, burn_total = 0, cook = 0;
    float pending_xp = 0.0f;
};

class LevelStorage {
public:
    LevelStorage(const std::string& world_dir);

    // level.dat and player data
    void save_level_dat(const World& world);
    bool load_level_dat(World& world);

    void save_player_dat(const Player& player);
    bool load_player_dat(Player& player);

    // Save all dirty chunks in the world. Called periodically.
    void save_all_dirty(World& world);

    // Day/night cycle persistence
    void save_time_of_day(float time_of_day);
    float load_time_of_day();

    // Block-entity persistence (furnaces).
    void save_furnaces(const std::vector<FurnaceSave>& furnaces);
    std::vector<FurnaceSave> load_furnaces();

    // Load a single chunk from its region file.
    void save_chunk(const Chunk& chunk, DimensionId dim);
    bool load_chunk(Chunk& chunk, DimensionId dim);

private:
    std::string m_world_dir;
    std::mutex m_mutex;

    std::string get_region_path(ChunkPos cp, DimensionId dim) const;
    std::string get_dimension_dir(DimensionId dim) const;
    RegionFile* get_or_open_region(ChunkPos cp, DimensionId dim);

    // Cache for open region files
    std::unordered_map<uint64_t, std::unique_ptr<RegionFile>> m_region_cache;


    static NbtTag chunk_to_nbt(const Chunk& chunk);
    static void nbt_to_chunk(const NbtTag& nbt, Chunk& chunk);
};

} // namespace mc

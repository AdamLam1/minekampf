#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gameplay/player.hpp" // GameMode

namespace mc {

enum class WorldSize : int32_t {
    Small = 8,      // 16x16 chunks
    Medium = 16,    // 32x32 chunks
};

struct WorldMeta {
    std::string name;        // folder name + display name
    std::string display_name;
    uint64_t seed = 0;
    GameMode game_mode = GameMode::Survival;
    WorldSize world_size = WorldSize::Small;
    int64_t created_at = 0;  // unix timestamp
    int64_t last_played = 0;
    int64_t time_played = 0; // seconds
    bool hardcore_locked = false;
};

// Manages save directory listing and per-world metadata (level.dat).
// Save location: %APPDATA%/.minekampf/saves/<world_name>/
class WorldManager {
public:
    [[nodiscard]] static std::string saves_dir();
    [[nodiscard]] static std::string world_dir(const std::string& name);

    // Scan the saves directory and return a list of all valid worlds.
    [[nodiscard]] static std::vector<WorldMeta> list_worlds();

    // Create a new world directory with level.dat. Returns false on failure.
    static bool create_world(const WorldMeta& meta);
    static bool delete_world(const std::string& name);

    // Read/write level.dat metadata for a world.
    [[nodiscard]] static bool load_meta(const std::string& world_dir, WorldMeta& out);
    static bool save_meta(const std::string& world_dir, const WorldMeta& meta);

    // Sanitize user-provided name → safe folder name.
    [[nodiscard]] static std::string sanitize_name(const std::string& input);

    // Hash a text seed to a 64-bit integer. Empty seed → random.
    [[nodiscard]] static uint64_t parse_seed(const std::string& text);

    [[nodiscard]] static std::string game_mode_str(GameMode m);
    [[nodiscard]] static std::string world_size_str(WorldSize s);
};

} // namespace mc
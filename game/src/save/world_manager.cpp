#include "save/world_manager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <algorithm>
#include <cstring>

#include "save/nbt.hpp"
#include "core/logger.hpp"

namespace mc {

namespace {
std::string appdata_base() {
    const char* appdata = getenv("APPDATA");
    return appdata ? std::string(appdata) + "/.minekampf" : ".minekampf";
}
}

std::string WorldManager::saves_dir() {
    return appdata_base() + "/saves";
}

std::string WorldManager::world_dir(const std::string& name) {
    return saves_dir() + "/" + name;
}

std::vector<WorldMeta> WorldManager::list_worlds() {
    std::vector<WorldMeta> worlds;
    std::string base = saves_dir();
    
    try {
        if (!std::filesystem::exists(base) || !std::filesystem::is_directory(base)) return worlds;

        for (const auto& entry : std::filesystem::directory_iterator(base)) {
            if (!entry.is_directory()) continue;
            std::string folder = entry.path().filename().string();
            std::string lwdat = entry.path().string() + "/level.dat";
            if (!std::filesystem::exists(lwdat)) continue;

            WorldMeta meta;
            if (load_meta(entry.path().string(), meta)) {
                worlds.push_back(meta);
            }
        }
    } catch (...) {
        MC_LOG_ERROR("Exception caught while listing worlds in {}", base);
    }

    // Sort: most recently played first
    std::sort(worlds.begin(), worlds.end(), [](const WorldMeta& a, const WorldMeta& b) {
        return a.last_played > b.last_played;
    });
    return worlds;
}

bool WorldManager::create_world(const WorldMeta& meta) {
    std::string dir = world_dir(meta.name);
    if (std::filesystem::exists(dir)) return false;
    std::filesystem::create_directories(dir);
    std::filesystem::create_directories(dir + "/region");
    return save_meta(dir, meta);
}

bool WorldManager::delete_world(const std::string& name) {
    std::string dir = world_dir(name);
    if (!std::filesystem::exists(dir)) return false;
    std::filesystem::remove_all(dir);
    return true;
}

bool WorldManager::load_meta(const std::string& world_dir_path, WorldMeta& out) {
    std::string path = world_dir_path + "/level.dat";
    if (!std::filesystem::exists(path)) return false;

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    size_t size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(size);
    in.read(reinterpret_cast<char*>(bytes.data()), size);

    try {
        auto [name, root] = NbtSerializer::deserialize(bytes);
        if (root.type != NbtTagType::Compound) return false;
        const auto& root_comp = *std::get<std::unique_ptr<NbtCompound>>(root.value);
        if (!root_comp.contains("Data")) return false;
        const auto& data = *std::get<std::unique_ptr<NbtCompound>>(root_comp.at("Data").value);

        if (data.contains("LevelName")) out.display_name = std::get<std::string>(data.at("LevelName").value);
        if (data.contains("RandomSeed")) out.seed = static_cast<uint64_t>(std::get<int64_t>(data.at("RandomSeed").value));
        if (data.contains("GameType")) out.game_mode = static_cast<GameMode>(std::get<int32_t>(data.at("GameType").value));
        if (data.contains("WorldSize")) out.world_size = static_cast<WorldSize>(std::get<int32_t>(data.at("WorldSize").value));
        if (data.contains("CreatedAt")) out.created_at = std::get<int64_t>(data.at("CreatedAt").value);
        if (data.contains("LastPlayed")) out.last_played = std::get<int64_t>(data.at("LastPlayed").value);
        if (data.contains("TimePlayed")) out.time_played = std::get<int64_t>(data.at("TimePlayed").value);
        if (data.contains("HardcoreLocked")) out.hardcore_locked = std::get<int8_t>(data.at("HardcoreLocked").value) != 0;

        out.name = std::filesystem::path(world_dir_path).filename().string();
        return true;
    } catch (...) {
        MC_LOG_ERROR("Failed to parse level.dat for {}", world_dir_path);
        return false;
    }
}

bool WorldManager::save_meta(const std::string& world_dir_path, const WorldMeta& meta) {
    NbtTag data = NbtTag::Compound();
    auto& comp = *std::get<std::unique_ptr<NbtCompound>>(data.value);

    comp["LevelName"] = NbtTag(meta.display_name.empty() ? meta.name : meta.display_name);
    comp["RandomSeed"] = NbtTag(static_cast<int64_t>(meta.seed));
    comp["GameType"] = NbtTag(static_cast<int32_t>(meta.game_mode));
    comp["WorldSize"] = NbtTag(static_cast<int32_t>(meta.world_size));
    comp["CreatedAt"] = NbtTag(meta.created_at);
    comp["LastPlayed"] = NbtTag(meta.last_played);
    comp["TimePlayed"] = NbtTag(meta.time_played);
    comp["HardcoreLocked"] = NbtTag(static_cast<int8_t>(meta.hardcore_locked ? 1 : 0));

    NbtTag root = NbtTag::Compound();
    auto& root_comp = *std::get<std::unique_ptr<NbtCompound>>(root.value);
    root_comp["Data"] = std::move(data);

    std::vector<uint8_t> bytes = NbtSerializer::serialize("", root);
    std::string path = world_dir_path + "/level.dat";
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return out.good();
}

std::string WorldManager::sanitize_name(const std::string& input) {
    std::string out;
    for (char c : input) {
        if (c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '_' || c == '-') {
            out.push_back(c);
        }
    }
    if (out.empty()) out = "world";
    return out;
}

uint64_t WorldManager::parse_seed(const std::string& text) {
    if (text.empty()) {
        std::random_device rd;
        std::mt19937_64 rng(rd());
        return rng();
    }
    // Try parsing as a number first
    try {
        size_t pos;
        uint64_t val = std::stoull(text, &pos);
        if (pos == text.size()) return val;
    } catch (...) {}
    // FNV-1a hash of the string
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (char c : text) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

std::string WorldManager::game_mode_str(GameMode m) {
    switch (m) {
        case GameMode::Survival: return "Survival";
        case GameMode::Creative: return "Creative";
        case GameMode::Hardcore: return "Hardcore";
        default: return "?";
    }
}

std::string WorldManager::world_size_str(WorldSize s) {
    switch (s) {
        case WorldSize::Small: return "16x16";
        case WorldSize::Medium: return "32x32";
        default: return "?";
    }
}

} // namespace mc
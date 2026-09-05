#include "gameplay/entity.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "core/logger.hpp"
#include "renderer/geo_model.hpp"

namespace fs = std::filesystem;

namespace mc {

namespace {

// Built-in species, ids 0..3. Stats mirror the original hardcoded spawner
// values so existing behavior is unchanged.
std::vector<MobSpec> builtin_specs() {
    std::vector<MobSpec> v;
    MobSpec zombie;
    zombie.id = 0; zombie.name = "zombie"; zombie.display_name = "Zombie";
    zombie.model_path = "assets/models/mobs/zombie.geo.json";
    zombie.texture_path = "assets/models/mobs/zombie.png";
    zombie.hostile = true; zombie.health = 20.0f; zombie.speed = 0.06f;
    zombie.attack_damage = 3.0f; zombie.xp_reward = 5; zombie.zombie_arms = true;
    zombie.builtin = true;
    v.push_back(std::move(zombie));

    MobSpec skeleton;
    skeleton.id = 1; skeleton.name = "skeleton"; skeleton.display_name = "Skeleton";
    skeleton.model_path = "assets/models/mobs/skeleton.geo.json";
    skeleton.texture_path = "assets/models/mobs/skeleton.png";
    skeleton.hostile = true; skeleton.health = 16.0f; skeleton.speed = 0.07f;
    skeleton.attack_damage = 2.0f; skeleton.xp_reward = 5;
    skeleton.builtin = true;
    v.push_back(std::move(skeleton));

    MobSpec cow;
    cow.id = 2; cow.name = "cow"; cow.display_name = "Cow";
    cow.model_path = "assets/models/mobs/cow.geo.json";
    cow.texture_path = "assets/models/mobs/cow.png";
    cow.health = 10.0f; cow.speed = 0.05f; cow.xp_reward = 2; cow.quadruped = true;
    cow.body_width = 0.9f; cow.body_height = 1.4f;
    cow.builtin = true;
    v.push_back(std::move(cow));

    MobSpec pig;
    pig.id = 3; pig.name = "pig"; pig.display_name = "Pig";
    pig.model_path = "assets/models/mobs/pig.geo.json";
    pig.texture_path = "assets/models/mobs/pig.png";
    pig.health = 10.0f; pig.speed = 0.05f; pig.xp_reward = 2; pig.quadruped = true;
    pig.body_width = 0.9f; pig.body_height = 0.9f;
    pig.builtin = true;
    v.push_back(std::move(pig));

    return v;
}

std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string capitalize(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

// Applies a sidecar <name>.mob.json (all keys optional).
bool apply_sidecar(const std::string& path, MobSpec& spec, std::string* err) {
    std::ifstream in(path);
    if (!in) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        if (err) *err = std::string(path) + ": JSON parse: " + e.what();
        return false;
    }
    if (!j.is_object()) {
        if (err) *err = path + ": expected a JSON object";
        return false;
    }
    spec.display_name = j.value("display_name", spec.display_name);
    spec.hostile = j.value("hostile", spec.hostile);
    spec.health = j.value("health", spec.health);
    spec.speed = j.value("speed", spec.speed);
    spec.attack_damage = j.value("attack_damage", spec.attack_damage);
    spec.follow_range = j.value("follow_range", spec.follow_range);
    spec.scale = j.value("scale", spec.scale);
    spec.body_width = j.value("body_width", spec.body_width);
    spec.body_height = j.value("body_height", spec.body_height);
    spec.quadruped = j.value("quadruped", spec.quadruped);
    spec.zombie_arms = j.value("zombie_arms", spec.zombie_arms);
    spec.xp_reward = j.value("xp", spec.xp_reward);
    spec.drop_item = lower_ascii(j.value("drop_item", spec.drop_item));
    spec.drop_min = j.value("drop_min", spec.drop_min);
    spec.drop_max = j.value("drop_max", spec.drop_max);
    if (spec.scale <= 0.0f) spec.scale = 1.0f;
    if (spec.health <= 0.0f) spec.health = 10.0f;
    if (spec.body_width <= 0.0f) spec.body_width = 0.6f;
    if (spec.body_height <= 0.0f) spec.body_height = 1.8f;
    return true;
}

} // namespace

MobRegistry::MobRegistry() { reset_to_builtin(); }

MobRegistry& MobRegistry::instance() {
    static MobRegistry reg;
    return reg;
}

void MobRegistry::reset_to_builtin() { specs_ = builtin_specs(); }

bool MobRegistry::scan_directory(const std::string& dir, std::string* err) {
    reset_to_builtin();
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        if (err) *err = "no such directory: " + dir;
        return false;
    }

    // Collect candidate model files; sort by stem so custom ids are stable
    // regardless of filesystem enumeration order. Note: ".geo.json" is a
    // double extension, so match on the filename, not path().extension().
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = lower_ascii(entry.path().filename().string());
        bool is_geo = fname.size() > 9 &&
                      fname.compare(fname.size() - 9, 9, ".geo.json") == 0;
        bool is_bb = fname.size() > 8 &&
                     fname.compare(fname.size() - 8, 8, ".bbmodel") == 0;
        if (is_geo || is_bb) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end(),
              [](const fs::path& a, const fs::path& b) { return a.string() < b.string(); });

    for (const auto& path : files) {
        std::string stem = lower_ascii(path.stem().string());
        // "<name>.geo.json" stems to "<name>.geo"; strip that too.
        if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".geo") == 0)
            stem.resize(stem.size() - 4);

        if (stem.empty()) continue;
        bool is_builtin = false;
        for (const auto& s : specs_) {
            if (s.name == stem) is_builtin = true;
        }
        if (is_builtin) continue; // sidecar-less replacement of built-ins is
                                  // already handled by the renderer loading
                                  // files from disk; no duplicate species.
        if (specs_.size() >= kMaxMobSpecies) {
            MC_LOG_WARN("MobRegistry: species limit reached, ignoring {}", stem);
            continue;
        }

        // Validate the model parses before registering it.
        std::string parse_err;
        auto geo = GeoModel::load_from_file(path.string(), &parse_err);
        if (!geo) {
            MC_LOG_WARN("MobRegistry: skipping {}: {}", path.filename().string(), parse_err);
            continue;
        }

        MobSpec spec;
        spec.id = static_cast<uint8_t>(specs_.size());
        spec.name = stem;
        spec.display_name = capitalize(stem);
        spec.model_path = path.string();
        // Sidecar png wins; otherwise the renderer falls back to a texture
        // embedded in the .bbmodel (or the procedural rig when neither).
        std::string png = (path.parent_path() / (stem + ".png")).string();
        if (fs::exists(png, ec)) spec.texture_path = png;

        std::string sidecar_err;
        std::string sidecar = (path.parent_path() / (stem + ".mob.json")).string();
        if (fs::exists(sidecar, ec) &&
            !apply_sidecar(sidecar, spec, &sidecar_err)) {
            MC_LOG_WARN("MobRegistry: bad sidecar {}: {}", sidecar, sidecar_err);
            continue;
        }
        specs_.push_back(std::move(spec));
        MC_LOG_INFO("MobRegistry: custom mob '{}' ({}{}) from {}", stem,
                    spec.hostile ? "hostile" : "passive",
                    spec.texture_path.empty() ? ", embedded texture" : "",
                    path.filename().string());
    }
    return true;
}

const MobSpec* MobRegistry::by_id(uint8_t id) const {
    if (id >= specs_.size()) return nullptr;
    return &specs_[id];
}

const MobSpec* MobRegistry::find(std::string_view name) const {
    std::string key = lower_ascii(std::string(name));
    for (const auto& s : specs_) {
        if (s.name == key) return &s;
    }
    return nullptr;
}

std::vector<const MobSpec*> MobRegistry::customs() const {
    std::vector<const MobSpec*> out;
    for (const auto& s : specs_) {
        if (!s.builtin) out.push_back(&s);
    }
    return out;
}

const MobSpec* MobRegistry::random_custom(bool hostile, uint32_t tick) const {
    std::vector<const MobSpec*> pool;
    for (const auto& s : specs_) {
        if (!s.builtin && s.hostile == hostile) pool.push_back(&s);
    }
    if (pool.empty()) return nullptr;
    return pool[tick % pool.size()];
}

void MobRegistry::apply_spec(Mob& m, const MobSpec& spec) {
    m.type = static_cast<MobType>(spec.id);
    m.max_health = spec.health;
    m.health = spec.health;
    m.speed = spec.speed;
    m.attack_damage = spec.attack_damage;
    m.follow_range = spec.follow_range;
    m.body_width = spec.body_width;
    m.body_height = spec.body_height;
    m.xp_reward = spec.xp_reward;
    m.goal_selector = GoalSelector{}; // clear builtin defaults
    if (spec.hostile) {
        m.goal_selector.add_goal(2, std::make_shared<MeleeAttackGoal>());
        m.goal_selector.add_goal(4, std::make_shared<LookAtPlayerGoal>());
        m.goal_selector.add_goal(6, std::make_shared<WanderGoal>());
    } else {
        m.goal_selector.add_goal(4, std::make_shared<LookAtPlayerGoal>());
        m.goal_selector.add_goal(6, std::make_shared<WanderGoal>());
    }
}

} // namespace mc

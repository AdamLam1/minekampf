#include "core/mod_manager.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/event_bus.hpp"
#include "core/logger.hpp"
#include "gameplay/item.hpp"
#include "world/block.hpp"

namespace mc {

namespace fs = std::filesystem;

ModLoadReport ModManager::last_report_{};

void ModManager::init_mods() {
    MC_LOG_INFO("Initializing ModManager API...");

    // Register some example hooks (acting as our Modding API)
    EventBus::get().subscribe<BlockBreakEvent>([](const BlockBreakEvent& e) {
        MC_LOG_INFO("[Mod API] Block broken at {}, {}, {} (ID: {})", e.x, e.y, e.z, e.block_id);
    });

    EventBus::get().subscribe<BlockPlaceEvent>([](const BlockPlaceEvent& e) {
        MC_LOG_INFO("[Mod API] Block placed at {}, {}, {} (ID: {})", e.x, e.y, e.z, e.block_id);
    });
}

namespace {

bool read_file(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

} // namespace

bool ModManager::parse_recipes_json(const std::string& text,
                                    std::vector<Recipe>& out,
                                    std::string& err) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        err = std::string("JSON parse error: ") + e.what();
        return false;
    }

    nlohmann::json array = doc;
    // Accept either a bare array or {"recipes": [...]}.
    if (doc.is_object() && doc.contains("recipes")) array = doc["recipes"];
    if (!array.is_array()) {
        err = "top level must be an array of recipes (or {\"recipes\": [...]})";
        return false;
    }

    for (size_t i = 0; i < array.size(); ++i) {
        const auto& entry = array[i];
        auto fail = [&](const std::string& why) {
            err = "recipe[" + std::to_string(i) + "]: " + why;
            return false;
        };
        if (!entry.is_object()) return fail("not an object");

        std::string type = entry.value("type", "shaped");
        std::string out_name = entry.value("output", "");
        int count = entry.value("count", 1);
        if (out_name.empty()) return fail("missing \"output\" item name");
        if (count < 1 || count > 64) return fail("count out of range 1..64");

        ItemId out_id = ItemRegistry::id_from_name(out_name);
        if (out_id == ITEM_AIR) return fail("unknown output item \"" + out_name + "\"");

        Recipe r;
        r.output = ItemStack(out_id, static_cast<uint8_t>(count));

        if (type == "shaped") {
            r.type = RecipeType::Shaped;
            if (!entry.contains("pattern") || !entry["pattern"].is_array() ||
                entry["pattern"].empty() || entry["pattern"].size() > 3) {
                return fail("shaped recipe needs pattern array of 1..3 rows");
            }
            for (const auto& row : entry["pattern"]) {
                if (!row.is_string()) return fail("pattern rows must be strings");
                r.pattern.push_back(row.get<std::string>());
            }
            size_t width = r.pattern[0].size();
            for (const auto& row : r.pattern) {
                if (row.size() != width) return fail("pattern rows must share one width");
            }
            if (width > 3 || r.pattern.size() > 3) return fail("pattern larger than 3x3");
            if (!entry.contains("ingredients") || !entry["ingredients"].is_object()) {
                return fail("shaped recipe needs ingredients object");
            }
            for (auto it = entry["ingredients"].begin(); it != entry["ingredients"].end(); ++it) {
                std::string key = it.key();
                if (key.size() != 1 || key == " ") return fail("ingredient key must be one char");
                ItemId ing = ItemRegistry::id_from_name(it.value().get<std::string>());
                if (ing == ITEM_AIR) {
                    return fail("unknown ingredient \"" + it.value().get<std::string>() + "\"");
                }
                r.ingredients[key[0]] = ing;
            }
            out.push_back(std::move(r));
        } else if (type == "shapeless") {
            r.type = RecipeType::Shapeless;
            if (!entry.contains("items") || !entry["items"].is_array() ||
                entry["items"].empty() || entry["items"].size() > 9) {
                return fail("shapeless recipe needs items array of 1..9");
            }
            for (const auto& name : entry["items"]) {
                if (!name.is_string()) return fail("shapeless items must be strings");
                ItemId ing = ItemRegistry::id_from_name(name.get<std::string>());
                if (ing == ITEM_AIR) {
                    return fail("unknown ingredient \"" + name.get<std::string>() + "\"");
                }
                r.shapeless_ingredients.push_back(ing);
            }
            out.push_back(std::move(r));
        } else {
            return fail("unknown type \"" + type + "\" (expected shaped|shapeless)");
        }
    }
    if (out.empty()) {
        err = "recipe document contains no recipes";
        return false;
    }
    return true;
}

bool ModManager::parse_quests_json(const std::string& text,
                                   std::vector<quest::QuestDef>& out,
                                   std::string& err) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        err = std::string("JSON parse error: ") + e.what();
        return false;
    }

    nlohmann::json array = doc;
    // Accept either a bare array or {"quests": [...]}.
    if (doc.is_object() && doc.contains("quests")) array = doc["quests"];
    if (!array.is_array()) {
        err = "top level must be an array of quests (or {\"quests\": [...]})";
        return false;
    }

    for (size_t i = 0; i < array.size(); ++i) {
        const auto& entry = array[i];
        auto fail = [&](const std::string& why) {
            err = "quest[" + std::to_string(i) + "]: " + why;
            return false;
        };
        if (!entry.is_object()) return fail("not an object");

        int id = entry.value("id", -1);
        if (id < 0) return fail("missing or negative \"id\"");
        std::string title = entry.value("title", "");
        if (title.empty()) return fail("missing \"title\"");
        std::string type = entry.value("type", "collect");

        quest::QuestDef q;
        q.id = id;
        q.title = title;
        q.description = entry.value("description", "");
        q.xp_reward = entry.value("xp", 0);

        if (type == "collect") {
            q.goal_type = quest::GoalType::Collect;
            std::string item_name = entry.value("item", "");
            q.collect_item = ItemRegistry::id_from_name(item_name);
            if (q.collect_item == ITEM_AIR) {
                return fail("unknown collect item \"" + item_name + "\"");
            }
            q.collect_count = entry.value("count", 1);
            if (q.collect_count < 1) return fail("count must be >= 1");
        } else if (type == "kill") {
            q.goal_type = quest::GoalType::Kill;
            std::string mob_name = entry.value("mob", "");
            if (!quest::mob_from_name(mob_name, q.kill_type)) {
                return fail("unknown mob \"" + mob_name + "\"");
            }
            q.kill_count = entry.value("count", 1);
            if (q.kill_count < 1) return fail("count must be >= 1");
        } else {
            return fail("unknown type \"" + type + "\" (expected collect|kill)");
        }

        if (entry.contains("rewards")) {
            if (!entry["rewards"].is_array()) return fail("rewards must be an array");
            for (const auto& r : entry["rewards"]) {
                if (!r.is_object()) return fail("rewards entries must be objects");
                std::string item_name = r.value("item", "");
                ItemId item = ItemRegistry::id_from_name(item_name);
                if (item == ITEM_AIR) return fail("unknown reward item \"" + item_name + "\"");
                int count = r.value("count", 1);
                if (count < 1 || count > 64) return fail("reward count out of range 1..64");
                q.item_rewards.push_back(ItemStack(item, static_cast<uint8_t>(count)));
            }
        }

        out.push_back(std::move(q));
    }
    if (out.empty()) {
        err = "quest document contains no quests";
        return false;
    }
    return true;
}

ModLoadReport ModManager::load_content(const std::string& dir) {
    ModLoadReport report;

    // Relative paths resolve against CWD, which differs between dev runs and
    // the packaged binary; try the obvious neighbors before giving up.
    std::vector<fs::path> candidates{fs::path(dir)};
    if (fs::path(dir).is_relative()) {
        candidates.emplace_back("../../../mods");
        candidates.emplace_back("../mods");
        candidates.emplace_back("../../../../game/mods");
    }

    fs::path root;
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec) && fs::is_directory(c, ec)) {
            root = c;
            break;
        }
    }
    if (root.empty()) {
        report.errors.push_back("mods directory not found: " + dir);
        last_report_ = report;
        MC_LOG_WARN("ModManager: no mods directory found");
        return report;
    }

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        report.files_scanned++;

        std::string text;
        if (!read_file(entry.path(), text)) {
            report.errors.push_back(entry.path().string() + ": unreadable");
            continue;
        }

        std::vector<Recipe> parsed;
        std::string err;
        if (parse_recipes_json(text, parsed, err)) {
            for (auto& recipe : parsed) {
                bool overridden = RecipeManager::add_or_override(std::move(recipe));
                if (overridden) ++report.recipes_overridden;
                else ++report.recipes_added;
            }
            MC_LOG_INFO("ModManager: {} loaded ({} recipes)", entry.path().string(), parsed.size());
            continue;
        }

        // Not a recipe document — try quests before giving up.
        std::vector<quest::QuestDef> parsed_quests;
        std::string quest_err;
        if (parse_quests_json(text, parsed_quests, quest_err)) {
            for (auto& q : parsed_quests) {
                bool overridden = quest::add_or_override(std::move(q));
                if (overridden) ++report.quests_overridden;
                else ++report.quests_added;
            }
            MC_LOG_INFO("ModManager: {} loaded ({} quests)", entry.path().string(), parsed_quests.size());
            continue;
        }

        report.errors.push_back(entry.path().string() + ": " + err + " / " + quest_err);
        MC_LOG_WARN("ModManager: {} skipped ({} / {})",
                    entry.path().string(), err, quest_err);
    }

    last_report_ = report;
    MC_LOG_INFO("ModManager: {} file(s), {} recipes added, {} overridden, {} quests added, "
                "{} overridden, {} error(s)",
                report.files_scanned, report.recipes_added, report.recipes_overridden,
                report.quests_added, report.quests_overridden, report.errors.size());
    return report;
}

} // namespace mc

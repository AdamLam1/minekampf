#pragma once

#include <string>
#include <vector>

#include "gameplay/crafting.hpp"
#include "gameplay/quest.hpp"

namespace mc {

// Result of one ModManager::load_content pass.
struct ModLoadReport {
    int files_scanned = 0;
    int recipes_added = 0;
    int recipes_overridden = 0;
    int quests_added = 0;
    int quests_overridden = 0;
    std::vector<std::string> errors; // one line per skipped file/entry
};

// Data-driven mod content loader. Mods are JSON files inside the `mods/`
// directory; today they define crafting recipes that OVERRIDE built-ins
// with the same output item.
//
// File format (array of recipe objects):
// [
//   {"type": "shaped", "output": {"item": "stick", "count": 9},
//    "pattern": ["X", "X"], "ingredients": {"X": "oak_planks"}},
//   {"type": "shapeless", "output": {"item": "glass", "count": 2},
//    "items": ["sand", "coal"]}
// ]
// Item names come from the block/item registries (case-insensitive).
class ModManager {
public:
    static void init_mods();

    // Scans `dir` for *.json mod files and registers recipes. Never throws:
    // broken files are recorded in the report and skipped. Candidates tried
    // in order when `dir` is relative and does not exist.
    static ModLoadReport load_content(const std::string& dir = "mods");

    // Parses one JSON document into recipes (unit-test entry point).
    // Returns false and fills `err` on any structural problem.
    static bool parse_recipes_json(const std::string& text,
                                   std::vector<Recipe>& out,
                                   std::string& err);

    // Parses one JSON document into quests (unit-test entry point):
    // {"quests": [{"id", "title", "description", "type": "collect|kill",
    //              "item", "count", "mob", "xp",
    //              "rewards": [{"item", "count"}]}]}
    // Accepts also a bare array of quest objects.
    static bool parse_quests_json(const std::string& text,
                                  std::vector<quest::QuestDef>& out,
                                  std::string& err);

    [[nodiscard]] static const ModLoadReport& last_report() { return last_report_; }

private:
    static ModLoadReport last_report_;
};

} // namespace mc

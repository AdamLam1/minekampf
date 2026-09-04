#pragma once

// Quest logic: a small catalog of fetch/kill quests, progress tracking and
// turn-in. Pure gameplay logic (PlayerInventory/Player only, no engine deps)
// so the whole state machine is unit tested directly (tests/test_quest).

#include <string>
#include <vector>

#include "gameplay/entity.hpp"
#include "gameplay/inventory.hpp"
#include "gameplay/item.hpp"
#include "world/block.hpp"

namespace mc {
class Player; // quest.hpp avoids including player.hpp (circular)
} // namespace mc

namespace mc::quest {

enum class GoalType : uint8_t {
    Collect, // bring N of an item to the NPC (items removed at turn-in)
    Kill,    // kill N of a mob species (counted live)
};

enum class State : uint8_t {
    NotStarted = 0,
    Active = 1,
    ReadyToTurnIn = 2,
    Completed = 3,
};

struct QuestDef {
    int id;
    std::string title;
    std::string description;   // ASCII-only (bitmap font)
    GoalType goal_type;
    ItemId collect_item = ITEM_AIR;
    int collect_count = 0;
    MobType kill_type = MobType::Zombie;
    int kill_count = 0;
    int xp_reward = 0;
    std::vector<ItemStack> item_rewards;
};

// Quest bookkeeping persisted inside Player (player.dat).
struct Progress {
    int quest_id = -1; // -1 = no quest taken
    State state = State::NotStarted;
    int progress = 0;  // kills for Kill quests; 0 for Collect until turn-in
};

// Mutable quest registry: built-in catalog plus mod JSON additions/overrides
// (loaded by ModManager after init_recipes). Keyed by quest id.
std::vector<QuestDef>& registry();

[[nodiscard]] inline const std::vector<QuestDef>& catalog() { return registry(); }

// Adds or replaces (by id) a quest. Returns true when it replaced one.
bool add_or_override(QuestDef def);

// "zombie" -> MobType::Zombie etc. (case-insensitive); false when unknown.
bool mob_from_name(std::string_view name, MobType& out);

[[nodiscard]] inline const QuestDef* by_id(int id) {
    for (const auto& q : catalog()) {
        if (q.id == id) return &q;
    }
    return nullptr;
}

[[nodiscard]] inline const QuestDef* active_def(const Progress& p) {
    return (p.state == State::Active || p.state == State::ReadyToTurnIn)
               ? by_id(p.quest_id)
               : nullptr;
}

// Accepts a quest. Fails while another quest is unfinished or this one is
// already completed/active.
inline bool accept(Progress& p, const QuestDef& def) {
    if (p.state == State::Active || p.state == State::ReadyToTurnIn) return false;
    if (p.quest_id == def.id && p.state == State::Completed) return false;
    p.quest_id = def.id;
    p.state = State::Active;
    p.progress = 0;
    return true;
}

// Kill hook. Returns true when this kill completed the objective.
inline bool on_kill(Progress& p, const QuestDef& def, MobType type) {
    if (p.state != State::Active || def.goal_type != GoalType::Kill) return false;
    if (p.quest_id != def.id || type != def.kill_type) return false;
    if (p.progress >= def.kill_count) return false;
    ++p.progress;
    if (p.progress >= def.kill_count) {
        p.state = State::ReadyToTurnIn;
        return true;
    }
    return false;
}

// Convenience overload resolving the def from the stored id.
inline bool on_kill(Progress& p, MobType type) {
    const QuestDef* def = active_def(p);
    return def && on_kill(p, *def, type);
}

[[nodiscard]] inline int count_in_inventory(const PlayerInventory& inv, ItemId item) {
    int n = 0;
    for (size_t i = 0; i < inv.size(); ++i) {
        const ItemStack s = inv.get_slot(i);
        if (s.item == item) n += s.count;
    }
    return n;
}

// Collect quests are turn-in-able once the player carries enough of the item.
[[nodiscard]] inline bool can_turn_in(const Progress& p, const QuestDef& def,
                                      const PlayerInventory& inv) {
    // Kill quests reach ReadyToTurnIn via on_kill; Collect stays Active until
    // the goods are in the inventory.
    if (p.state != State::Active && p.state != State::ReadyToTurnIn) return false;
    if (p.quest_id != def.id) return false;
    if (def.goal_type == GoalType::Kill) {
        return p.progress >= def.kill_count; // state already ReadyToTurnIn
    }
    return count_in_inventory(inv, def.collect_item) >= def.collect_count;
}

// Completes the quest: removes collect items, grants XP + item rewards.
// Returns false (and changes nothing) when requirements are not met.
// Defined in quest.cpp (needs the complete Player type).
bool turn_in(Progress& p, const QuestDef& def, Player& player);

// Human-readable objective line for HUD/dialog, e.g. "Zlota ruda 2/3".
[[nodiscard]] inline std::string progress_text(const Progress& p) {
    const QuestDef* def = by_id(p.quest_id);
    if (!def) return "";
    switch (p.state) {
        case State::NotStarted: return def->title + " (nie rozpoczete)";
        case State::Active: {
            if (def->goal_type == GoalType::Kill) {
                return def->title + ": " + std::to_string(p.progress) + "/" +
                       std::to_string(def->kill_count) + " zabitych";
            }
            return def->title + ": przynies " +
                   std::to_string(def->collect_count) + " sztuk";
        }
        case State::ReadyToTurnIn: return def->title + " - do oddania!";
        case State::Completed: return def->title + " (ukonczone)";
    }
    return "";
}

} // namespace mc::quest

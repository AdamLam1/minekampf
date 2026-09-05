#include "gameplay/quest.hpp"

#include <algorithm>
#include <cctype>

#include "gameplay/player.hpp"
#include "gameplay/survival.hpp"

namespace mc::quest {

std::vector<QuestDef>& registry() {
    static std::vector<QuestDef> kCatalog = {
        {0, "Gornicza przysluga",
         "Przynies 3 zlote rudy.\nNagroda: 15 XP i 2 jablka.",
         GoalType::Collect, static_cast<ItemId>(BLOCK_GOLD_ORE), 3,
         MobType::Zombie, 0, 15, {ItemStack(ITEM_APPLE, 2)}},
        {1, "Nocna straz",
         "Zabij 2 zombie.\nNagroda: 30 XP i 6 strzal.",
         GoalType::Kill, ITEM_AIR, 0,
         MobType::Zombie, 2, 30, {ItemStack(ITEM_ARROW, 6)}},
    };
    return kCatalog;
}

bool add_or_override(QuestDef def) {
    auto& reg = registry();
    auto it = std::find_if(reg.begin(), reg.end(),
                           [&](const QuestDef& q) { return q.id == def.id; });
    if (it != reg.end()) {
        *it = std::move(def);
        return true;
    }
    reg.push_back(std::move(def));
    return false;
}

bool mob_from_name(std::string_view name, MobType& out) {
    // Registry lookup covers the built-ins plus any custom Blockbench species.
    if (const MobSpec* spec = MobRegistry::instance().find(name)) {
        out = static_cast<MobType>(spec->id);
        return true;
    }
    return false;
}

bool turn_in(Progress& p, const QuestDef& def, Player& player) {
    if (!can_turn_in(p, def, player.inventory)) return false;

    if (def.goal_type == GoalType::Collect) {
        int remaining = def.collect_count;
        for (size_t i = 0; i < player.inventory.size() && remaining > 0; ++i) {
            ItemStack s = player.inventory.get_slot(i);
            if (s.item != def.collect_item) continue;
            int take = (s.count < remaining) ? s.count : remaining;
            s.count = static_cast<uint8_t>(s.count - take);
            remaining -= take;
            if (s.count == 0) s.item = ITEM_AIR;
            player.inventory.set_slot(i, s);
        }
    }

    survival::gain_xp(player, def.xp_reward);
    for (ItemStack reward : def.item_rewards) {
        player.inventory.add_item_to_main(reward);
    }
    p.state = State::Completed;
    return true;
}

} // namespace mc::quest

#include <gtest/gtest.h>
#include "gameplay/quest.hpp"
#include "save/level_storage.hpp"

#include <filesystem>

namespace mc {

namespace {
Player fresh_player() {
    Player p;
    p.mode = GameMode::Survival;
    return p;
}
} // namespace

TEST(QuestTest, CatalogHasTwoQuests) {
    EXPECT_EQ(quest::catalog().size(), 2u);
    EXPECT_NE(quest::by_id(0), nullptr);
    EXPECT_NE(quest::by_id(1), nullptr);
    EXPECT_EQ(quest::by_id(0)->goal_type, quest::GoalType::Collect);
    EXPECT_EQ(quest::by_id(1)->goal_type, quest::GoalType::Kill);
}

TEST(QuestTest, AcceptRejectsWhileAnotherActive) {
    Player p = fresh_player();
    EXPECT_TRUE(quest::accept(p.quest, *quest::by_id(0)));
    EXPECT_EQ(p.quest.state, quest::State::Active);
    EXPECT_FALSE(quest::accept(p.quest, *quest::by_id(1)));
    EXPECT_EQ(p.quest.quest_id, 0);
}

TEST(QuestTest, CollectTurnInRemovesItemsAndGrantsRewards) {
    Player p = fresh_player();
    const quest::QuestDef* def = quest::by_id(0); // 3 gold ore -> 15xp + 2 apples
    ASSERT_NE(def, nullptr);
    EXPECT_TRUE(quest::accept(p.quest, *def));

    // Not enough yet.
    p.inventory.set_slot(0, ItemStack(static_cast<ItemId>(BLOCK_GOLD_ORE), 2));
    EXPECT_FALSE(quest::turn_in(p.quest, *def, p));

    p.inventory.set_slot(0, ItemStack(static_cast<ItemId>(BLOCK_GOLD_ORE), 3));
    EXPECT_TRUE(quest::can_turn_in(p.quest, *def, p.inventory));
    int xp_before = p.xp_total;
    ASSERT_TRUE(quest::turn_in(p.quest, *def, p));

    EXPECT_EQ(p.quest.state, quest::State::Completed);
    EXPECT_EQ(p.xp_total, xp_before + def->xp_reward);
    EXPECT_FALSE(quest::count_in_inventory(p.inventory, static_cast<ItemId>(BLOCK_GOLD_ORE)));
    // Reward apples arrived.
    EXPECT_EQ(quest::count_in_inventory(p.inventory, ITEM_APPLE), 2);
}

TEST(QuestTest, CollectSpansMultipleSlots) {
    Player p = fresh_player();
    const quest::QuestDef* def = quest::by_id(0);
    quest::accept(p.quest, *def);
    const ItemId ore = static_cast<ItemId>(BLOCK_GOLD_ORE);
    p.inventory.set_slot(0, ItemStack(ore, 2));
    p.inventory.set_slot(5, ItemStack(ore, 1));
    EXPECT_TRUE(quest::turn_in(p.quest, *def, p));
    EXPECT_EQ(quest::count_in_inventory(p.inventory, ore), 0);
}

TEST(QuestTest, KillQuestCountsOnlyMatchingSpecies) {
    Player p = fresh_player();
    const quest::QuestDef* def = quest::by_id(1); // kill 2 zombies
    quest::accept(p.quest, *def);

    EXPECT_FALSE(quest::on_kill(p.quest, MobType::Skeleton)); // wrong species
    EXPECT_EQ(p.quest.progress, 0);

    EXPECT_FALSE(quest::on_kill(p.quest, MobType::Zombie)); // 1/2
    EXPECT_EQ(p.quest.progress, 1);
    EXPECT_EQ(p.quest.state, quest::State::Active);

    EXPECT_TRUE(quest::on_kill(p.quest, MobType::Zombie)); // 2/2 -> ready
    EXPECT_EQ(p.quest.state, quest::State::ReadyToTurnIn);

    EXPECT_FALSE(quest::on_kill(p.quest, MobType::Zombie)); // no overcount
    EXPECT_EQ(p.quest.progress, 2);
}

TEST(QuestTest, KillQuestTurnInGrantsRewardsOnce) {
    Player p = fresh_player();
    const quest::QuestDef* def = quest::by_id(1);
    quest::accept(p.quest, *def);
    quest::on_kill(p.quest, MobType::Zombie);
    quest::on_kill(p.quest, MobType::Zombie);

    int xp_before = p.xp_total;
    EXPECT_TRUE(quest::turn_in(p.quest, *def, p));
    EXPECT_EQ(p.xp_total, xp_before + def->xp_reward);
    EXPECT_EQ(quest::count_in_inventory(p.inventory, ITEM_ARROW), 6);

    // Completed quest cannot be turned in again.
    EXPECT_FALSE(quest::turn_in(p.quest, *def, p));
    EXPECT_EQ(quest::count_in_inventory(p.inventory, ITEM_ARROW), 6);
}

TEST(QuestTest, ProgressTextCoversStates) {
    Player p = fresh_player();
    quest::accept(p.quest, *quest::by_id(1));
    EXPECT_NE(quest::progress_text(p.quest).find("0/2"), std::string::npos);
    quest::on_kill(p.quest, MobType::Zombie);
    quest::on_kill(p.quest, MobType::Zombie);
    EXPECT_NE(quest::progress_text(p.quest).find("do oddania"), std::string::npos);
    quest::turn_in(p.quest, *quest::by_id(1), p);
    EXPECT_NE(quest::progress_text(p.quest).find("ukonczone"), std::string::npos);
}

TEST(QuestTest, QuestSurvivesPlayerDatRoundTrip) {
    namespace fs = std::filesystem;
    std::string dir = "test_world_quest";
    fs::remove_all(dir);
    fs::create_directories(dir);

    {
        LevelStorage storage(dir);
        Player out;
        out.mode = GameMode::Survival;
        quest::accept(out.quest, *quest::by_id(1));
        quest::on_kill(out.quest, MobType::Zombie); // 1/2
        out.health = 13.5f;
        storage.save_player_dat(out);
    }
    {
        LevelStorage storage(dir);
        Player in;
        ASSERT_TRUE(storage.load_player_dat(in));
        EXPECT_EQ(in.quest.quest_id, 1);
        EXPECT_EQ(in.quest.state, quest::State::Active);
        EXPECT_EQ(in.quest.progress, 1);
        EXPECT_FLOAT_EQ(in.health, 13.5f);
    }
    fs::remove_all(dir);
}

} // namespace mc

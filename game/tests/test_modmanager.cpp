#include <gtest/gtest.h>
#include "core/mod_manager.hpp"
#include "gameplay/crafting.hpp"
#include "gameplay/smelting.hpp"
#include "world/block.hpp"

#include <filesystem>
#include <fstream>

namespace mc {

namespace {
const char* kValid = R"JSON([
  {"type": "shaped", "output": "stick", "count": 9,
   "pattern": ["X", "X"], "ingredients": {"X": "oak_planks"}},
  {"type": "shapeless", "output": "glass", "count": 2, "items": ["sand", "coal"]}
])JSON";
} // namespace

// ---- Parser ----

TEST(ModManagerTest, ParsesShapedAndShapelessByName) {
    std::vector<Recipe> out;
    std::string err;
    ASSERT_TRUE(ModManager::parse_recipes_json(kValid, out, err)) << err;
    ASSERT_EQ(out.size(), 2u);

    EXPECT_EQ(out[0].type, RecipeType::Shaped);
    EXPECT_EQ(out[0].output.item, ITEM_STICK);
    EXPECT_EQ(out[0].output.count, 9);
    ASSERT_EQ(out[0].pattern.size(), 2u);
    EXPECT_EQ(out[0].pattern[0], "X");
    EXPECT_EQ(out[0].ingredients['X'], ITEM_OAK_PLANKS);

    EXPECT_EQ(out[1].type, RecipeType::Shapeless);
    EXPECT_EQ(out[1].output.item, static_cast<ItemId>(BLOCK_GLASS));
    EXPECT_EQ(out[1].output.count, 2);
    ASSERT_EQ(out[1].shapeless_ingredients.size(), 2u);
}

TEST(ModManagerTest, RejectsStructurallyBrokenDocuments) {
    std::vector<Recipe> out;
    std::string err;

    EXPECT_FALSE(ModManager::parse_recipes_json("{not json", out, err));

    err.clear();
    EXPECT_FALSE(ModManager::parse_recipes_json(
        R"([{"output": "stick", "pattern": ["X"], "ingredients": {"X": "no_such_item"}}])",
        out, err));
    EXPECT_NE(err.find("no_such_item"), std::string::npos);

    err.clear();
    EXPECT_FALSE(ModManager::parse_recipes_json(
        R"([{"type": "shaped", "pattern": ["X"], "ingredients": {"X": "stick"}}])",
        out, err)); // missing output
    EXPECT_NE(err.find("output"), std::string::npos);

    err.clear();
    EXPECT_FALSE(ModManager::parse_recipes_json(
        R"([{"type": "alchemy", "output": "stick"}])", out, err));
    EXPECT_NE(err.find("alchemy"), std::string::npos);

    err.clear();
    EXPECT_FALSE(ModManager::parse_recipes_json(
        R"([{"type": "shaped", "output": "stick", "count": 99,
            "pattern": ["X"], "ingredients": {"X": "stick"}}])",
        out, err));
    EXPECT_NE(err.find("count"), std::string::npos);
}

TEST(ModManagerTest, ParsesSmeltingRecipes) {
    std::vector<Recipe> out;
    std::string err;
    ASSERT_TRUE(ModManager::parse_recipes_json(
        R"([{"type": "smelting", "input": "iron_ore", "output": "iron_ingot",
            "count": 2, "xp": 0.9}])",
        out, err)) << err;
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].type, RecipeType::Smelting);
    EXPECT_EQ(out[0].smelting_input, static_cast<ItemId>(BLOCK_IRON_ORE));
    EXPECT_EQ(out[0].output.item, ITEM_IRON_INGOT);
    EXPECT_EQ(out[0].output.count, 2);
    EXPECT_FLOAT_EQ(out[0].xp, 0.9f);

    // Unknown input/output and bad xp are rejected.
    EXPECT_FALSE(ModManager::parse_recipes_json(
        R"([{"type": "smelting", "input": "no_such_ore", "output": "iron_ingot"}])",
        out, err));
    EXPECT_NE(err.find("unknown input"), std::string::npos);
    EXPECT_FALSE(ModManager::parse_recipes_json(
        R"([{"type": "smelting", "output": "iron_ingot"}])", out, err));
    EXPECT_NE(err.find("input"), std::string::npos);
}

TEST(ModManagerTest, SmeltingRecipesRouteIntoRuntimeTable) {
    // Registering a custom recipe makes the furnace accept a new input.
    smelting::register_custom_smelt(ITEM_STONE, {static_cast<ItemId>(BLOCK_GLASS), 0.2f});
    auto r = smelting::smelt_result(ITEM_STONE);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->output, static_cast<ItemId>(BLOCK_GLASS));
    EXPECT_FLOAT_EQ(r->xp, 0.2f);
    // Re-registration with the same input overrides the earlier one.
    smelting::register_custom_smelt(ITEM_STONE, {static_cast<ItemId>(BLOCK_ICE), 0.5f});
    r = smelting::smelt_result(ITEM_STONE);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->output, static_cast<ItemId>(BLOCK_ICE));
    // Built-in recipes still resolve.
    auto iron = smelting::smelt_result(static_cast<ItemId>(BLOCK_IRON_ORE));
    ASSERT_TRUE(iron.has_value());
    EXPECT_EQ(iron->output, ITEM_IRON_INGOT);
}

TEST(ModManagerTest, NameLookupIsCaseInsensitiveAndCoversBlocks) {
    // Block names map to the block id (drops use the same convention); the
    // pure item "diamond" is a different registry entry.
    EXPECT_EQ(ItemRegistry::id_from_name("DIAMOND_ORE"),
              static_cast<ItemId>(BLOCK_DIAMOND_ORE));
    EXPECT_EQ(ItemRegistry::id_from_name("diamond"), ITEM_DIAMOND);
    EXPECT_EQ(ItemRegistry::id_from_name("Cooked_Meat"), ITEM_COOKED_MEAT);
    EXPECT_EQ(ItemRegistry::id_from_name("definitely_not_an_item"), ITEM_AIR);
}

// ---- Override semantics ----

TEST(ModManagerTest, AddOrOverrideReplacesBuiltinWithSameOutput) {
    RecipeManager::init_recipes();
    struct Reset { ~Reset() { RecipeManager::init_recipes(); } } reset;
    size_t builtin_count = RecipeManager::get_all_recipes().size();
    ASSERT_GT(builtin_count, 0u);

    // Sticks builtin: 4 per craft. Mod says 9.
    std::vector<Recipe> parsed;
    std::string err;
    ASSERT_TRUE(ModManager::parse_recipes_json(
        R"([{"type": "shaped", "output": "stick", "count": 9,
            "pattern": ["X", "X"], "ingredients": {"X": "oak_planks"}}])",
        parsed, err));
    EXPECT_TRUE(RecipeManager::add_or_override(parsed[0]));
    EXPECT_EQ(RecipeManager::get_all_recipes().size(), builtin_count); // replaced, not appended

    CraftingGrid grid;
    grid.items[0] = ItemStack(ITEM_OAK_PLANKS);
    grid.items[3] = ItemStack(ITEM_OAK_PLANKS);
    auto match = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->output.item, ITEM_STICK);
    EXPECT_EQ(match->output.count, 9); // mod wins over builtin 4
}

TEST(ModManagerTest, AddOrOverrideAppendsNewOutputs) {
    RecipeManager::init_recipes();
    struct Reset { ~Reset() { RecipeManager::init_recipes(); } } reset;
    size_t before = RecipeManager::get_all_recipes().size();
    Recipe fresh;
    fresh.type = RecipeType::Shapeless;
    fresh.output = ItemStack(ITEM_APPLE, 5);
    fresh.shapeless_ingredients = {ITEM_STICK};
    EXPECT_FALSE(RecipeManager::add_or_override(std::move(fresh))); // nothing replaced
    EXPECT_EQ(RecipeManager::get_all_recipes().size(), before + 1);
}

// ---- Directory loading ----

TEST(ModManagerTest, LoadContentScansDirAndSkipsBrokenFiles) {
    namespace fs = std::filesystem;
    std::string dir = "test_mods_dir";
    fs::remove_all(dir);
    fs::create_directories(dir);

    { std::ofstream f(dir + "/good.json"); f << kValid; }
    { std::ofstream f(dir + "/broken.json"); f << "{\"recipes\": [ ]}"; }
    { std::ofstream f(dir + "/readme.txt"); f << "not json"; }

    RecipeManager::init_recipes();
    struct Reset { ~Reset() { RecipeManager::init_recipes(); } } reset;
    ModLoadReport report = ModManager::load_content(dir);

    EXPECT_EQ(report.files_scanned, 2); // txt ignored, broken json scanned
    ASSERT_EQ(report.errors.size(), 1u);
    EXPECT_NE(report.errors[0].find("no recipes"), std::string::npos);
    EXPECT_EQ(report.recipes_added, 1);       // glass recipe is new
    EXPECT_EQ(report.recipes_overridden, 1);  // stick recipe replaces builtin

    // The glass recipe must now be craftable in the live registry.
    CraftingGrid grid;
    grid.items[0] = ItemStack(static_cast<ItemId>(BLOCK_SAND));
    grid.items[1] = ItemStack(ITEM_COAL);
    auto match = RecipeManager::find_matching_recipe(grid);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->output.item, static_cast<ItemId>(BLOCK_GLASS));
    EXPECT_EQ(match->output.count, 2);

    fs::remove_all(dir);
}

TEST(ModManagerTest, LoadContentMissingDirIsReportedNotFatal) {
    RecipeManager::init_recipes();
    ModLoadReport report = ModManager::load_content("definitely_missing_mods_dir");
    EXPECT_EQ(report.files_scanned, 0);
    EXPECT_EQ(report.recipes_added, 0);
    ASSERT_FALSE(report.errors.empty());
}

// ---- Quest documents ----

TEST(ModManagerTest, ParsesQuestDocuments) {
    const char* quests_json = R"JSON({
      "quests": [
        {"id": 10, "title": "Test collect", "description": "desc",
         "type": "collect", "item": "iron_ore", "count": 4, "xp": 20,
         "rewards": [{"item": "apple", "count": 2}]},
        {"id": 11, "title": "Test kill", "type": "kill",
         "mob": "SKELETON", "count": 3, "xp": 30}
      ]
    })JSON";

    std::vector<quest::QuestDef> out;
    std::string err;
    ASSERT_TRUE(ModManager::parse_quests_json(quests_json, out, err)) << err;
    ASSERT_EQ(out.size(), 2u);

    EXPECT_EQ(out[0].id, 10);
    EXPECT_EQ(out[0].goal_type, quest::GoalType::Collect);
    EXPECT_EQ(out[0].collect_item, static_cast<ItemId>(BLOCK_IRON_ORE));
    EXPECT_EQ(out[0].collect_count, 4);
    EXPECT_EQ(out[0].xp_reward, 20);
    ASSERT_EQ(out[0].item_rewards.size(), 1u);
    EXPECT_EQ(out[0].item_rewards[0].item, ITEM_APPLE);

    EXPECT_EQ(out[1].goal_type, quest::GoalType::Kill);
    EXPECT_EQ(out[1].kill_type, MobType::Skeleton);
    EXPECT_EQ(out[1].kill_count, 3);
}

TEST(ModManagerTest, RejectsBrokenQuestDocuments) {
    std::vector<quest::QuestDef> out;
    std::string err;

    EXPECT_FALSE(ModManager::parse_quests_json("{nope", out, err));

    err.clear();
    EXPECT_FALSE(ModManager::parse_quests_json(
        R"([{"id": 1, "title": "x", "type": "collect", "item": "no_such_block"}])", out, err));
    EXPECT_NE(err.find("no_such_block"), std::string::npos);

    err.clear();
    EXPECT_FALSE(ModManager::parse_quests_json(
        R"([{"id": 5, "title": "x", "type": "kill", "mob": "dragon"}])", out, err));
    EXPECT_NE(err.find("dragon"), std::string::npos);

    err.clear();
    EXPECT_FALSE(ModManager::parse_quests_json(
        R"([{"id": 6, "title": "x", "type": "alchemy"}])", out, err));
    EXPECT_NE(err.find("alchemy"), std::string::npos);
}

TEST(ModManagerTest, QuestRegistryAddOrOverrideAndReset) {
    quest::QuestDef fresh;
    fresh.id = 9001;
    fresh.title = "Mod quest";
    fresh.goal_type = quest::GoalType::Kill;
    fresh.kill_type = MobType::Pig;
    fresh.kill_count = 1;
    EXPECT_FALSE(quest::add_or_override(fresh)); // appended
    EXPECT_NE(quest::by_id(9001), nullptr);

    fresh.kill_count = 2;
    EXPECT_TRUE(quest::add_or_override(fresh)); // replaced
    EXPECT_EQ(quest::by_id(9001)->kill_count, 2);

    // Remove the test quest again so other suites see the clean catalog.
    auto& reg = quest::registry();
    reg.erase(std::remove_if(reg.begin(), reg.end(),
                             [](const quest::QuestDef& q) { return q.id == 9001; }),
              reg.end());
}

TEST(ModManagerTest, BuiltinQuestCatalogIntact) {
    // Built-ins survive unless a mod JSON overrides them by id.
    ASSERT_GE(quest::catalog().size(), 2u);
    EXPECT_EQ(quest::by_id(0)->goal_type, quest::GoalType::Collect);
    EXPECT_EQ(quest::by_id(1)->goal_type, quest::GoalType::Kill);

    quest::QuestDef override_q = *quest::by_id(0);
    override_q.collect_count = 99;
    EXPECT_TRUE(quest::add_or_override(override_q));
    EXPECT_EQ(quest::by_id(0)->collect_count, 99);
    EXPECT_EQ(quest::catalog().size(), 2u); // replaced, not appended

    // Restore the builtin values.
    override_q.collect_count = 3;
    quest::add_or_override(override_q);
}

} // namespace mc

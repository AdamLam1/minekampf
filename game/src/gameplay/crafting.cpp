#include "gameplay/crafting.hpp"
#include "world/block.hpp"

#include <algorithm>

namespace mc {

namespace {
std::vector<Recipe> g_recipes;

struct TrimmedGrid {
    int width;
    int height;
    std::vector<ItemStack> items;
};

TrimmedGrid trim_empty_rows_cols(const CraftingGrid& grid) {
    int min_x = 3, max_x = -1;
    int min_y = 3, max_y = -1;

    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            if (!grid.items[y * 3 + x].is_empty()) {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
    }

    if (max_x == -1) {
        return {0, 0, {}};
    }

    int width = max_x - min_x + 1;
    int height = max_y - min_y + 1;
    std::vector<ItemStack> items;
    items.reserve(width * height);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            items.push_back(grid.items[y * 3 + x]);
        }
    }

    return {width, height, items};
}

bool check_match(const TrimmedGrid& grid, const Recipe& recipe, bool mirror) {
    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            int pattern_x = mirror ? (grid.width - 1 - x) : x;
            char c = recipe.pattern[y][pattern_x];
            
            const ItemStack& grid_item = grid.items[y * grid.width + x];
            
            if (c == ' ') {
                if (!grid_item.is_empty()) return false;
            } else {
                auto it = recipe.ingredients.find(c);
                if (it == recipe.ingredients.end()) return false;
                if (grid_item.item != it->second) return false;
            }
        }
    }
    return true;
}
} // namespace

void RecipeManager::init_recipes() {
    g_recipes.clear();

    auto add_shaped = [&](ItemStack out, std::vector<std::string> pattern,
                          std::unordered_map<char, ItemId> ingredients) {
        Recipe r;
        r.type = RecipeType::Shaped;
        r.output = out;
        r.pattern = std::move(pattern);
        r.ingredients = std::move(ingredients);
        g_recipes.push_back(std::move(r));
    };
    auto add_shapeless = [&](ItemStack out, std::vector<ItemId> ingredients) {
        Recipe r;
        r.type = RecipeType::Shapeless;
        r.output = out;
        r.shapeless_ingredients = std::move(ingredients);
        g_recipes.push_back(std::move(r));
    };

    // --- Wood chain ---
    // Spruce/birch logs exist only as raw block ids (no dedicated item ids).
    const auto spruce_log = static_cast<ItemId>(BLOCK_SPRUCE_LOG);
    const auto birch_log = static_cast<ItemId>(BLOCK_BIRCH_LOG);
    add_shapeless(ItemStack(ITEM_OAK_PLANKS, 4), {ITEM_OAK_LOG});
    add_shapeless(ItemStack(ITEM_OAK_PLANKS, 4), {spruce_log});
    add_shapeless(ItemStack(ITEM_OAK_PLANKS, 4), {birch_log});

    // Crafting table: one stick of planks per side -> simplified 2x2 planks.
    add_shaped(ItemStack(BLOCK_CRAFTING_TABLE, 1),
               {"XX", "XX"}, {{'X', ITEM_OAK_PLANKS}});

    add_shaped(ItemStack(ITEM_STICK, 4), {"X", "X"}, {{'X', ITEM_OAK_PLANKS}});

    // --- Torches (coal over stick -> 4) ---
    add_shaped(ItemStack(BLOCK_TORCH, 4),
               {"C", "|"}, {{'C', ITEM_COAL}, {'|', ITEM_STICK}});

    // --- Furnace: ring of 8 cobblestone (empty center) ---
    add_shaped(ItemStack(BLOCK_FURNACE, 1),
               {"XXX", "X X", "XXX"}, {{'X', ITEM_COBBLESTONE}});

    // --- Bow: sticks bent around plant-fiber string (tall grass substitute;
    //     spider silk does not exist yet) ---
    const auto fiber = static_cast<ItemId>(BLOCK_TALL_GRASS);
    add_shaped(ItemStack(ITEM_BOW, 1),
               {"|XX", "| X", "|XX"},
               {{'|', fiber}, {'X', ITEM_STICK}});

    // --- Arrows: fiber fletching over a stick shaft (-> 4) ---
    add_shaped(ItemStack(ITEM_ARROW, 4),
               {"F", "|"},
               {{'F', fiber}, {'|', ITEM_STICK}});

    // --- Tools: pickaxes ---
    const struct {
        ItemId material;
        ItemId output;
    } pick_mats[] = {
        {ITEM_OAK_PLANKS, ITEM_WOODEN_PICKAXE},
        {ITEM_COBBLESTONE, ITEM_STONE_PICKAXE},
        {ITEM_IRON_INGOT, ITEM_IRON_PICKAXE},
        {ITEM_DIAMOND, ITEM_DIAMOND_PICKAXE},
    };
    for (const auto& m : pick_mats) {
        add_shaped(ItemStack(m.output, 1),
                   {"XXX", " | ", " | "},
                   {{'X', m.material}, {'|', ITEM_STICK}});
    }

    // --- Weapons: swords ---
    const struct {
        ItemId material;
        ItemId output;
    } sword_mats[] = {
        {ITEM_OAK_PLANKS, ITEM_WOODEN_SWORD},
        {ITEM_COBBLESTONE, ITEM_STONE_SWORD},
        {ITEM_IRON_INGOT, ITEM_IRON_SWORD},
        {ITEM_DIAMOND, ITEM_DIAMOND_SWORD},
    };
    for (const auto& m : sword_mats) {
        add_shaped(ItemStack(m.output, 1),
                   {"X", "X", "|"},
                   {{'X', m.material}, {'|', ITEM_STICK}});
    }

    // --- Blocks from stone family ---
    add_shaped(ItemStack(BLOCK_BRICKS, 1),
               {"XX", "XX"}, {{'X', BLOCK_CLAY}});

    // --- Enchanting chain: book -> bookshelf -> enchanting table ---
    // Fiber (tall grass) stands in for paper.
    add_shapeless(ItemStack(ITEM_BOOK, 1), {fiber, fiber, fiber});
    add_shaped(ItemStack(BLOCK_BOOKSHELF, 1),
               {"PPP", "BBB", "PPP"},
               {{'P', ITEM_OAK_PLANKS}, {'B', ITEM_BOOK}});
    add_shaped(ItemStack(BLOCK_ENCHANTING_TABLE, 1),
               {" B ", "DOD", "OOO"},
               {{'B', ITEM_BOOK}, {'D', ITEM_DIAMOND}, {'O', BLOCK_OBSIDIAN}});

    // --- Redstone components ---
    add_shaped(ItemStack(BLOCK_LEVER_OFF, 1),
               {"|", "X"}, {{'|', ITEM_STICK}, {'X', ITEM_COBBLESTONE}});
    add_shaped(ItemStack(BLOCK_PRESSURE_PLATE_OFF, 1),
               {"XX"}, {{'X', ITEM_STONE}});
    add_shaped(ItemStack(BLOCK_REDSTONE_LAMP_OFF, 1),
               {" R ", "RGR", " R "},
               {{'R', static_cast<ItemId>(BLOCK_REDSTONE_WIRE)}, {'G', BLOCK_GLOWSTONE}});
    add_shaped(ItemStack(BLOCK_REPEATER_OFF, 1),
               {"TRT", "SSS"},
               {{'T', static_cast<ItemId>(BLOCK_REDSTONE_TORCH)},
                {'R', static_cast<ItemId>(BLOCK_REDSTONE_WIRE)},
                {'S', ITEM_STONE}});
}

bool RecipeManager::add_or_override(Recipe recipe) {
    bool replaced = false;
    g_recipes.erase(
        std::remove_if(g_recipes.begin(), g_recipes.end(),
                       [&](const Recipe& r) {
                           if (r.output.item != recipe.output.item) return false;
                           replaced = true;
                           return true;
                       }),
        g_recipes.end());
    g_recipes.push_back(std::move(recipe));
    return replaced;
}

std::optional<Recipe> RecipeManager::find_matching_recipe(const CraftingGrid& grid) {
    for (const auto& recipe : g_recipes) {
        if (recipe.type == RecipeType::Shaped) {
            if (match_shaped(grid, recipe)) return recipe;
        } else if (recipe.type == RecipeType::Shapeless) {
            if (match_shapeless(grid, recipe)) return recipe;
        }
    }
    return std::nullopt;
}

const std::vector<Recipe>& RecipeManager::get_all_recipes() {
    return g_recipes;
}

bool RecipeManager::match_shaped(const CraftingGrid& grid, const Recipe& recipe) {
    TrimmedGrid trimmed = trim_empty_rows_cols(grid);
    if (trimmed.width == 0) return false;

    int r_width = recipe.pattern[0].length();
    int r_height = recipe.pattern.size();

    if (trimmed.width != r_width || trimmed.height != r_height) {
        return false;
    }

    if (check_match(trimmed, recipe, false)) return true;
    if (check_match(trimmed, recipe, true)) return true;

    return false;
}

bool RecipeManager::match_shapeless(const CraftingGrid& grid, const Recipe& recipe) {
    std::vector<ItemId> grid_items;
    for (const auto& item : grid.items) {
        if (!item.is_empty()) {
            grid_items.push_back(item.item);
        }
    }

    if (grid_items.size() != recipe.shapeless_ingredients.size()) {
        return false;
    }

    std::vector<ItemId> r_items = recipe.shapeless_ingredients;
    std::sort(grid_items.begin(), grid_items.end());
    std::sort(r_items.begin(), r_items.end());

    return grid_items == r_items;
}

} // namespace mc

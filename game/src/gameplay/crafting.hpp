#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include "gameplay/item.hpp"

namespace mc {

enum class RecipeType {
    Shaped,
    Shapeless,
    Smelting
};

struct Recipe {
    RecipeType type;
    ItemStack output;

    // For Shaped:
    std::vector<std::string> pattern;
    std::unordered_map<char, ItemId> ingredients;

    // For Shapeless:
    std::vector<ItemId> shapeless_ingredients;

    // For Smelting:
    ItemId smelting_input = ITEM_AIR;
    float xp = 0.0f;
    int cook_time = 200; // in ticks (200 = 10s)
};

// Represents a 3x3 crafting grid. Empty slots are ITEM_AIR.
struct CraftingGrid {
    std::vector<ItemStack> items; // Size 9
    
    CraftingGrid() : items(9) {}
};

class RecipeManager {
public:
    static void init_recipes();

    // Data-driven (mod) registration: removes every builtin recipe producing
    // the same output item, then appends the mod's version. Returns true
    // when an existing recipe was replaced.
    static bool add_or_override(Recipe recipe);

    [[nodiscard]] static std::optional<Recipe> find_matching_recipe(const CraftingGrid& grid);

    [[nodiscard]] static const std::vector<Recipe>& get_all_recipes();

private:
    [[nodiscard]] static bool match_shaped(const CraftingGrid& grid, const Recipe& recipe);
    [[nodiscard]] static bool match_shapeless(const CraftingGrid& grid, const Recipe& recipe);
};

} // namespace mc

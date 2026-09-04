#include "gameplay/survival.hpp"

#include <algorithm>
#include <cmath>

namespace mc::survival {

void add_exhaustion(Player& p, float amount) {
    if (amount <= 0.0f) return;
    p.food_exhaustion += amount;

    // Overflow converts into saturation-then-hunger drain, chained so a big
    // burst (regen after starving) still drains multiple points.
    while (p.food_exhaustion >= EXHAUSTION_THRESHOLD) {
        p.food_exhaustion -= EXHAUSTION_THRESHOLD;
        if (p.food_saturation > 0.0f) {
            p.food_saturation = std::max(0.0f, p.food_saturation - 1.0f);
        } else if (p.food_level > 0) {
            --p.food_level;
        }
    }
}

void tick_hunger(Player& p, const HungerTickInput& input) {
    // Creative players are exempt from hunger entirely.
    if (p.mode == GameMode::Creative) return;

    if (input.sprinting) add_exhaustion(p, EXHAUSTION_SPRINT_PER_TICK);
    if (input.jumped) add_exhaustion(p, EXHAUSTION_JUMP);

    const bool fed = p.food_level >= 18;
    const bool wounded = p.health > 0.0f && p.health < p.max_health;

    if (fed && wounded) {
        ++p.food_regen_timer;
        if (p.food_regen_timer >= REGEN_INTERVAL_TICKS) {
            p.food_regen_timer = 0;
            p.health = std::min(p.max_health, p.health + 1.0f);
            add_exhaustion(p, REGEN_EXHAUSTION);
        }
    } else {
        p.food_regen_timer = 0;
    }

    if (p.food_level <= 0) {
        ++p.starve_timer;
        if (p.starve_timer >= STARVE_INTERVAL_TICKS) {
            p.starve_timer = 0;
            float floor = (p.mode == GameMode::Hardcore) ? 0.0f : STARVATION_FLOOR;
            p.health = std::max(floor, p.health - 1.0f);
        }
    } else {
        p.starve_timer = 0;
    }
}

int gain_xp(Player& p, int amount) {
    if (amount <= 0) return p.xp_total;
    p.xp_total += amount;

    int points_into_level =
        static_cast<int>(std::lround(p.xp_progress * static_cast<float>(xp_for_next_level(p.xp_level))));
    points_into_level += amount;

    int need = xp_for_next_level(p.xp_level);
    while (points_into_level >= need) {
        points_into_level -= need;
        ++p.xp_level;
        need = xp_for_next_level(p.xp_level);
    }
    p.xp_progress = std::clamp(
        static_cast<float>(points_into_level) / static_cast<float>(need), 0.0f, 1.0f);
    return p.xp_total;
}

int spend_xp_levels(Player& p, int levels) {
    if (levels <= 0) return 0;
    int spent = std::min(levels, p.xp_level);
    p.xp_level -= spent;
    p.xp_progress = 0.0f;
    return spent;
}

int eat_from_selected(Player& p) {
    ItemStack held = p.inventory.get_selected_item();
    if (held.is_empty()) return 0;

    auto food = mining::food_value(held.item);
    if (!food) return 0;

    held.count -= 1;
    if (held.count == 0) held.item = ITEM_AIR;
    p.inventory.set_slot(p.inventory.get_selected_hotbar_slot(), held);

    p.food_level = std::clamp(p.food_level + food->nutrition, 0, 20);
    p.food_saturation = std::clamp(p.food_saturation + food->saturation,
                                   0.0f, static_cast<float>(p.food_level));
    return food->nutrition;
}

} // namespace mc::survival

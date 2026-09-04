#pragma once

#include <algorithm>
#include <cmath>
#include "core/types.hpp"

namespace mc {

inline float calculate_attack_damage(float weapon_base, float cooldown_progress,
                                      bool is_critical, float enchant_bonus) {
    float damage = weapon_base;
    // Attack cooldown: 0.2 + progress^2 * 0.8
    float cooldown_mult = 0.2f + cooldown_progress * cooldown_progress * 0.8f;
    damage *= cooldown_mult;
    if (is_critical) damage *= 1.5f;
    damage += enchant_bonus;
    return damage;
}

inline float calculate_armor_reduction(float damage, float armor_points, float armor_toughness) {
    if (armor_points <= 0.0f) return 0.0f;
    // plan PHASE17 §5.2 formula
    float a = armor_points / 5.0f;
    float b = armor_points - damage / (2.0f + armor_toughness / 4.0f);
    float reduction = std::clamp(std::max(a, b), 0.0f, 20.0f) / 25.0f;
    return reduction;
}

inline float calculate_protection_reduction(float epf) {
    // EPF capped at 20 → max 80% reduction
    float capped = std::min(epf, 20.0f);
    return capped / 25.0f;
}

inline float calculate_knockback(float base_knockback, float enchant_level, bool sprinting,
                                   float target_resistance) {
    float kb = base_knockback + enchant_level * 0.5f;
    if (sprinting) kb += 0.5f;
    kb *= (1.0f - std::clamp(target_resistance, 0.0f, 1.0f));
    return kb;
}

// Sharpness/Smite/Bane enchantment bonus
inline float sharpness_bonus(int level) { return 0.5f * level + 0.5f; }
inline float smite_bonus(int level) { return 2.5f * level; }
inline float bane_bonus(int level) { return 2.5f * level; }

// Ranged enchantments
inline float power_bonus(int level) { return 0.5f * level + 0.5f; }
inline float impaling_bonus(int level) { return 2.5f * level; }

// EPF per enchantment type
inline float protection_epf(int level) { return static_cast<float>(level); }  // weight 1
inline float fire_protection_epf(int level) { return static_cast<float>(level) * 2.0f; }
inline float blast_protection_epf(int level) { return static_cast<float>(level) * 2.0f; }
inline float projectile_protection_epf(int level) { return static_cast<float>(level) * 2.0f; }

// XP level formula (PHASE13 §3.2)
inline int xp_for_level(int level) {
    if (level <= 16) return level * level + 6 * level;
    if (level <= 31) return static_cast<int>(2.5f * level * level - 40.5f * level + 360.0f);
    return static_cast<int>(4.5f * level * level - 162.5f * level + 2220.0f);
}

inline int xp_for_next_level(int level) {
    if (level <= 15) return 2 * level + 7;
    if (level <= 30) return 5 * level - 38;
    return 9 * level - 158;
}

} // namespace mc

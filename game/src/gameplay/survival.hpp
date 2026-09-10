#pragma once

// Pure survival-progression logic: hunger/exhaustion draining, natural
// regeneration, starvation damage, XP awarding, eating. No engine or GL
// dependencies — unit tested directly (see test_survival).

#include "gameplay/player.hpp"
#include "gameplay/combat.hpp"
#include "gameplay/mining.hpp"
#include "world/block.hpp"

namespace mc::survival {

// Exhaustion budget before it converts into a hunger/saturation point.
inline constexpr float EXHAUSTION_THRESHOLD = 4.0f;

// Tuned per-tick costs (20 ticks/second).
inline constexpr float EXHAUSTION_SPRINT_PER_TICK = 0.02f;
inline constexpr float EXHAUSTION_JUMP = 0.05f;
inline constexpr float EXHAUSTION_ATTACK_SWING = 0.1f;
inline constexpr float EXHAUSTION_MINE_BLOCK = 0.01f;
inline constexpr float REGEN_EXHAUSTION = 6.0f;

// Regeneration cadence: 1 HP every 4 s while fed.
inline constexpr int REGEN_INTERVAL_TICKS = 80;
// Starvation cadence: 1 damage every 4 s once food hits zero.
inline constexpr int STARVE_INTERVAL_TICKS = 80;
// Starvation floors here outside of Hardcore mode (vanilla behavior).
inline constexpr float STARVATION_FLOOR = 1.0f;

struct HungerTickInput {
    bool sprinting = false;
    bool jumped = false;
};

void add_exhaustion(Player& p, float amount);
void tick_hunger(Player& p, const HungerTickInput& input);

// Adds XP; updates level + progress via the PHASE17 curve. Returns new total.
int gain_xp(Player& p, int amount);

// Removes `levels` levels from the player (enchanting table cost).
// Progress within the level resets to 0. Returns levels actually spent.
int spend_xp_levels(Player& p, int levels);

[[nodiscard]] inline constexpr int xp_reward_for_block(BlockId broken) {
    switch (broken) {
        case BLOCK_COAL_ORE: return 1;
        case BLOCK_DIAMOND_ORE: return 7;
        default: return 0;
    }
}
[[nodiscard]] inline constexpr int xp_reward_for_kill(bool hostile) { return hostile ? 5 : 2; }

// Eats one unit of the currently held stack. Returns nutrition consumed,
// 0 if held item isn't food. Consumes the item on success.
int eat_from_selected(Player& p);

// ---------------------------------------------------------------------------
// Environmental hazards: fall damage, drowning, lava. Pure state machine —
// the game feeds per-tick world context, then applies result.total_damage()
// through the player damage path (feedback + death handling) itself.
struct EnvironmentTickInput {
    bool head_in_water = false;
    bool body_in_lava = false;
    bool in_water = false;      // any body part in water (resets fall tracking)
    bool on_ground = false;
    bool just_landed = false;   // became grounded this tick
    float fall_speed = 0.0f;    // downward speed this tick (positive = falling)
    bool creative_exempt = false;
};

struct EnvironmentTickResult {
    float fall_damage = 0.0f;   // applied on the landing tick
    float drown_damage = 0.0f;
    float lava_damage = 0.0f;
    [[nodiscard]] float total_damage() const {
        return fall_damage + drown_damage + lava_damage;
    }
};

// One hazard state-machine step (call once per game tick). Damage cadence:
// vanilla fall formula (1 HP per block beyond 3), 1 HP/s drowning after 15 s
// of air, 2 HP/s while in lava. Creative/spectator players only reset timers.
EnvironmentTickResult tick_environment(Player& p, const EnvironmentTickInput& in);

} // namespace mc::survival

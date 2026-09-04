#pragma once

#include <cstdint>
#include <queue>

#include "core/random.hpp"
#include "core/types.hpp"
#include "world/block.hpp"
#include "world/world.hpp"

namespace mc {

// Scheduled tick (PHASE5 §1.2): deterministic, time-delayed block updates.
struct ScheduledTick {
    BlockPos pos;
    BlockId block;     // expected block (verification)
    int64_t target_tick;
    int priority;

    bool operator>(const ScheduledTick& o) const {
        if (target_tick != o.target_tick) return target_tick > o.target_tick;
        if (priority != o.priority) return priority > o.priority;
        return false; // equal
    }
};

// Tick system (PHASE5 §1): scheduled ticks (priority queue) + random ticks
// (per-section stochastic). Fluid flow for water and lava.
class TickSystem {
public:
    void schedule(BlockPos pos, BlockId block, int64_t current_tick, int delay_ticks, int priority = 0);

    // Run all scheduled ticks due at `current_tick` (capped per tick).
    void process(World& world, int64_t current_tick, Rng& rng);

    // Process fluid blocks (water flow, lava spread, water+lava interaction).
    void process_fluids(World& world, int64_t current_tick, Rng& rng);

    // Run random ticks across loaded chunks (PHASE5 §1.3): grass spread,
    // snow/ice melt in bright light. Uses `rng` for position selection.
    void random_ticks(World& world, Rng& rng);

private:
    std::priority_queue<ScheduledTick, std::vector<ScheduledTick>, std::greater<ScheduledTick>> queue_;
};

} // namespace mc

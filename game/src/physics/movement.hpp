#pragma once

#include "core/types.hpp"
#include "gameplay/player.hpp"
#include "world/world.hpp"

namespace mc {

// Tick player physics (PHASE4 §3). Per-tick constants (20 Hz). Applies
// gravity/swim/fly, integrates input, and resolves voxel collision.
void tick_player(World& world, Player& player, const PlayerInput& input);

} // namespace mc

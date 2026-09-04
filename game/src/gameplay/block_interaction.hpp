#pragma once

#include "core/types.hpp"
#include "gameplay/player.hpp"
#include "physics/raycast.hpp"
#include "world/block.hpp"
#include "world/world.hpp"

namespace mc {

// Break the block at `pos` (sets air, marks neighbors dirty).
[[nodiscard]] bool break_block(World& world, BlockPos pos);

// Place `block` at `pos` if it's air or a replaceable liquid. Does NOT place
// inside the player's AABB (so you can't trap yourself).
[[nodiscard]] bool place_block(World& world, BlockPos pos, BlockId block, const Player& player);

// Raycast from the player's eye and break the hit block.
[[nodiscard]] std::optional<HitResult> interact_break(World& world, const Player& player, float reach);

// Raycast from the player's eye and place `player.selected_block` adjacent to
// the hit face.
[[nodiscard]] std::optional<HitResult> interact_place(World& world, const Player& player, float reach);

} // namespace mc

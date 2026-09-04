#pragma once

#include <unordered_map>
#include <vector>
#include "core/types.hpp"
#include "world/block.hpp"
#include "world/world.hpp"

namespace mc {

class RedstoneSystem {
public:
    // Process all redstone updates for this tick. Called once per server tick.
    // entity_positions drives pressure plates (player + mob feet positions).
    void tick(World& world, const std::vector<Vec3>& entity_positions = {});

    // Update power level at a specific wire position, returns true if changed.
    bool update_wire(World& world, BlockPos pos);

    // Get stored power level for a wire. Returns 0 for non-wire blocks.
    [[nodiscard]] uint8_t get_power(BlockPos pos) const;

    // Notify that a block at pos changed — recalculate neighbors.
    void on_block_changed(World& world, BlockPos pos);

    // Check if a block is powered by adjacent redstone (for pistons, doors, etc.)
    [[nodiscard]] bool is_powered(const World& world, BlockPos pos) const;

    // Any source adjacent to pos (lever/torch/block/repeater/plate).
    [[nodiscard]] bool is_source_adjacent(const World& world, BlockPos pos) const;

private:
    void propagate_wire(World& world, BlockPos pos, uint8_t power);

    // Register/refresh a component after placement or state change.
    void register_component(World& world, BlockPos pos);
    void update_lamp(World& world, BlockPos pos);

    // Block update ordering: WEST, EAST, DOWN, UP, NORTH, SOUTH
    static constexpr Direction UPDATE_ORDER[] = {
        Direction::West, Direction::East, Direction::Down,
        Direction::Up, Direction::North, Direction::South
    };

    std::unordered_map<BlockPos, uint8_t> m_power_levels;

    // Component tracking (block-position keyed).
    std::unordered_map<BlockPos, uint8_t> m_lamps_;        // pos -> 1 if lit
    std::unordered_map<BlockPos, int> m_repeaters_;        // pos -> ticks left before applying target
    std::unordered_map<BlockPos, uint8_t> m_repeater_target_; // pos -> 1 = turn ON pending
    std::unordered_map<BlockPos, int> m_plates_;           // pos -> off-delay ticks left (0 = pressed now)
};

} // namespace mc

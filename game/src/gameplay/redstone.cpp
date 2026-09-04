#include "gameplay/redstone.hpp"
#include "world/chunk.hpp"
#include <algorithm>

namespace mc {

namespace {

[[nodiscard]] bool is_wire(BlockId b) {
    return b >= BLOCK_REDSTONE_WIRE && b <= BLOCK_REDSTONE_WIRE_POWER_15;
}
[[nodiscard]] uint8_t wire_power_of(BlockId b) {
    if (b == BLOCK_REDSTONE_WIRE) return 0;
    if (is_wire(b)) return static_cast<uint8_t>(b - BLOCK_REDSTONE_WIRE_POWER_1 + 1);
    return 0;
}
// Fixed power sources: torch, redstone block, active lever, active repeater,
// pressed plate.
[[nodiscard]] bool is_power_source(BlockId b) {
    return b == BLOCK_REDSTONE_TORCH || b == BLOCK_REDSTONE_BLOCK ||
           b == BLOCK_LEVER_ON || b == BLOCK_REPEATER_ON ||
           b == BLOCK_PRESSURE_PLATE_ON;
}

} // namespace

uint8_t RedstoneSystem::get_power(BlockPos pos) const {
    auto it = m_power_levels.find(pos);
    if (it != m_power_levels.end()) return it->second;
    return 0;
}

bool RedstoneSystem::is_source_adjacent(const World& world, BlockPos pos) const {
    for (auto dir : UPDATE_ORDER) {
        if (is_power_source(world.get_block(pos + mc::offset(dir)))) return true;
    }
    return false;
}

bool RedstoneSystem::is_powered(const World& world, BlockPos pos) const {
    for (auto dir : UPDATE_ORDER) {
        BlockPos nb = pos + mc::offset(dir);
        BlockId b = world.get_block(nb);
        if (is_wire(b)) {
            if (get_power(nb) > 0) return true;
        }
        if (is_power_source(b)) return true;
    }
    return false;
}

bool RedstoneSystem::update_wire(World& world, BlockPos pos) {
    BlockId self = world.get_block(pos);
    if (!is_wire(self)) return false;

    uint8_t max_input = 0;

    for (auto dir : UPDATE_ORDER) {
        BlockPos nb = pos + mc::offset(dir);
        BlockId nid = world.get_block(nb);

        if (is_wire(nid)) {
            uint8_t np = get_power(nb);
            if (np > max_input) max_input = np;
        } else if (is_power_source(nid)) {
            max_input = 15;
        }
    }

    uint8_t new_power = (max_input > 0) ? static_cast<uint8_t>(std::max(0, max_input - 1)) : 0;
    uint8_t old_power = get_power(pos);

    if (new_power != old_power) {
        if (new_power > 0) {
            m_power_levels[pos] = new_power;
            // Update the block ID to reflect power level
            world.set_block(pos, static_cast<BlockId>(BLOCK_REDSTONE_WIRE_POWER_1 + new_power - 1));
        } else {
            m_power_levels.erase(pos);
            world.set_block(pos, BLOCK_REDSTONE_WIRE);
        }

        // Propagate to neighbors in update order
        for (auto dir : UPDATE_ORDER) {
            BlockPos nb = pos + mc::offset(dir);
            BlockId nid = world.get_block(nb);
            if (is_wire(nid)) {
                update_wire(world, nb);
            }
        }
        // Consumers that react to wire power changes.
        for (auto dir : UPDATE_ORDER) {
            BlockPos nb = pos + mc::offset(dir);
            BlockId nid = world.get_block(nb);
            if (is_redstone_lamp(nid)) {
                m_lamps_[nb] = 0; // force refresh this tick
            } else if (is_repeater(nid) && m_repeaters_.find(nb) == m_repeaters_.end()) {
                m_repeaters_[nb] = 0;
            }
        }
        return true;
    }
    return false;
}

void RedstoneSystem::propagate_wire(World& world, BlockPos pos, uint8_t power) {
    m_power_levels[pos] = power;
    if (power >= 2) {
        for (auto dir : UPDATE_ORDER) {
            BlockPos nb = pos + mc::offset(dir);
            BlockId nid = world.get_block(nb);
            if (is_wire(nid)) {
                uint8_t new_power = static_cast<uint8_t>(power - 1);
                if (new_power > get_power(nb)) {
                    propagate_wire(world, nb, new_power);
                }
            }
        }
    }
}

void RedstoneSystem::register_component(World& world, BlockPos pos) {
    BlockId b = world.get_block(pos);
    if (is_redstone_lamp(b)) {
        m_lamps_[pos] = 0;
    } else if (is_repeater(b)) {
        if (m_repeaters_.find(pos) == m_repeaters_.end()) m_repeaters_[pos] = 0;
    } else if (is_pressure_plate(b)) {
        m_plates_[pos] = 0;
    }
}

void RedstoneSystem::update_lamp(World& world, BlockPos pos) {
    BlockId b = world.get_block(pos);
    if (!is_redstone_lamp(b)) return;
    bool should_be_on = is_powered(world, pos);
    BlockId want = should_be_on ? BLOCK_REDSTONE_LAMP_ON : BLOCK_REDSTONE_LAMP_OFF;
    if (b != want) {
        world.set_block(pos, want);
        m_lamps_[pos] = should_be_on ? 1 : 0;
        on_block_changed(world, pos);
    }
}

void RedstoneSystem::on_block_changed(World& world, BlockPos pos) {
    BlockId b = world.get_block(pos);
    if (is_wire(b)) {
        update_wire(world, pos);
    }
    register_component(world, pos);
    // Also update neighbors that might be affected
    for (auto dir : UPDATE_ORDER) {
        BlockPos nb = pos + mc::offset(dir);
        BlockId nid = world.get_block(nb);
        if (is_wire(nid)) {
            update_wire(world, nb);
        } else if (is_redstone_lamp(nid)) {
            m_lamps_[nb] = 0;
        } else if (is_repeater(nid) || is_pressure_plate(nid)) {
            register_component(world, nb);
        }
    }
}

void RedstoneSystem::tick(World& world, const std::vector<Vec3>& entity_positions) {
    bool any_tracked = !m_power_levels.empty() || !m_lamps_.empty() ||
                       !m_repeaters_.empty() || !m_plates_.empty();
    if (!any_tracked) return;

    // --- Pressure plates: pressed while an entity stands on the cell. ---
    for (auto it = m_plates_.begin(); it != m_plates_.end();) {
        BlockPos pos = it->first;
        BlockId b = world.get_block(pos);
        if (!is_pressure_plate(b)) {
            it = m_plates_.erase(it);
            continue;
        }
        bool occupied = false;
        for (const Vec3& e : entity_positions) {
            if (e.x >= static_cast<float>(pos.x) && e.x < static_cast<float>(pos.x + 1) &&
                e.z >= static_cast<float>(pos.z) && e.z < static_cast<float>(pos.z + 1) &&
                e.y >= static_cast<float>(pos.y) && e.y < static_cast<float>(pos.y + 1)) {
                occupied = true;
                break;
            }
        }
        BlockId want = occupied ? BLOCK_PRESSURE_PLATE_ON : BLOCK_PRESSURE_PLATE_OFF;
        if (b != want) {
            world.set_block(pos, want);
            on_block_changed(world, pos);
            b = want;
        }
        // 1-tick off-delay keeps the map entry alive for lamps that just lost power.
        if (!occupied && it->second <= 0) {
            it = m_plates_.erase(it);
        } else {
            it->second = occupied ? 2 : it->second - 1;
            ++it;
        }
    }

    // --- Repeaters: latch input with a 2-tick delay. ---
    for (auto it = m_repeaters_.begin(); it != m_repeaters_.end();) {
        BlockPos pos = it->first;
        BlockId b = world.get_block(pos);
        if (!is_repeater(b)) {
            it = m_repeaters_.erase(it);
            m_repeater_target_.erase(pos);
            continue;
        }
        bool input = is_powered(world, pos);
        BlockId current_on = (b == BLOCK_REPEATER_ON);
        if (m_repeater_target_.count(pos) == 0 || (m_repeater_target_[pos] ? true : false) != input) {
            m_repeater_target_[pos] = input ? 1 : 0;
            it->second = 2; // delay in ticks
        }
        if (m_repeater_target_.count(pos)) {
            if (it->second > 0) {
                --it->second;
            } else if ((m_repeater_target_[pos] != 0) != current_on) {
                world.set_block(pos, input ? BLOCK_REPEATER_ON : BLOCK_REPEATER_OFF);
                on_block_changed(world, pos);
            }
        }
        ++it;
    }

    // --- Wires: recalculated each tick while sources exist. ---
    std::vector<BlockPos> positions;
    positions.reserve(m_power_levels.size());
    for (const auto& [bp, power] : m_power_levels) {
        positions.push_back(bp);
    }
    m_power_levels.clear();
    for (auto& bp : positions) {
        BlockId b = world.get_block(bp);
        if (is_wire(b)) {
            update_wire(world, bp);
        }
    }

    // --- Lamps: react to adjacent power. ---
    std::vector<BlockPos> lamp_positions;
    lamp_positions.reserve(m_lamps_.size());
    for (const auto& [bp, lit] : m_lamps_) lamp_positions.push_back(bp);
    for (auto& bp : lamp_positions) {
        update_lamp(world, bp);
    }
    // Forget lamps that settled OFF and stayed unpowered.
    for (auto it = m_lamps_.begin(); it != m_lamps_.end();) {
        BlockId b = world.get_block(it->first);
        if (!is_redstone_lamp(b)) {
            it = m_lamps_.erase(it);
        } else if (b == BLOCK_REDSTONE_LAMP_OFF) {
            it = m_lamps_.erase(it); // dormant until a neighbor changes
        } else {
            ++it;
        }
    }
}

} // namespace mc

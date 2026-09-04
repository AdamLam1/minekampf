#include "gameplay/tick_system.hpp"

#include <unordered_set>

#include "core/config.hpp"
#include "world/block.hpp"
#include "world/lighting.hpp"

namespace mc {

namespace {
constexpr int MAX_TICKS_PER_SERVER_TICK = 65536;
constexpr int WATER_FLOW_DELAY = 5;
constexpr int MAX_WATER_LEVEL = 7;
constexpr int MAX_LAVA_LEVEL = 3;

static const BlockPos H_SPREAD[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};

// Try to spread a fluid block. Returns the block placed or BLOCK_AIR.
// Newly placed fluid blocks schedule their own follow-up tick: the chunk
// activation scan only bootstraps flow, so a scan-ordered spread must not
// depend on a later re-scan to keep moving.
BlockId spread_fluid(TickSystem& ticks, World& world, BlockPos pos, BlockId block, Rng& rng,
                     int64_t current_tick) {
    bool is_water_block = is_water(block);
    int level = fluid_level(block);
    if (level < 0) return BLOCK_AIR;
    int max_level = is_water_block ? MAX_WATER_LEVEL : MAX_LAVA_LEVEL;

    BlockPos below{pos.x, pos.y - 1, pos.z};
    if (below.y >= MIN_Y) {
        BlockId below_id = world.get_block(below);

        // Water + lava interaction below
        if (is_water_block && is_lava(below_id)) {
            (void) world.set_block(below, is_fluid_source(below_id) ? BLOCK_OBSIDIAN : BLOCK_COBBLESTONE);
            if (!is_fluid_source(block)) (void) world.set_block(pos, BLOCK_AIR);
            return BLOCK_COBBLESTONE;
        }
        if (is_lava(block) && is_water(below_id)) {
            (void) world.set_block(below, is_fluid_source(below_id) ? BLOCK_OBSIDIAN : BLOCK_COBBLESTONE);
            if (!is_fluid_source(block)) (void) world.set_block(pos, BLOCK_AIR);
            return BLOCK_COBBLESTONE;
        }

        if (below_id == BLOCK_AIR) {
            int new_level = level + 1;
            if (new_level > max_level) new_level = max_level;
            BlockId new_block = is_water_block ? water_for_level(new_level) : BLOCK_LAVA;
            (void) world.set_block(below, new_block);
            ticks.schedule(below, new_block, current_tick, WATER_FLOW_DELAY);
            if (!is_fluid_source(block)) (void) world.set_block(pos, BLOCK_AIR);
            return new_block;
        }
    }

    // Horizontal spread: only if block below is solid.
    BlockId below_id = world.get_block({pos.x, pos.y - 1, pos.z});
    if (below_id == BLOCK_AIR || is_fluid(below_id)) return BLOCK_AIR;
    if (level >= max_level) return BLOCK_AIR;

    int order[4] = {0, 1, 2, 3};
    for (int i = 3; i > 0; --i) {
        int j = rng.next_int(i + 1);
        int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }

    BlockId result = BLOCK_AIR;
    for (int i = 0; i < 4; ++i) {
        BlockPos np = pos + H_SPREAD[order[i]];
        BlockId nb = world.get_block(np);
        // Water + lava interaction
        if (is_water_block && is_lava(nb)) {
            (void) world.set_block(np, is_fluid_source(nb) ? BLOCK_OBSIDIAN : BLOCK_COBBLESTONE);
            result = BLOCK_COBBLESTONE;
            continue;
        }
        if (is_lava(block) && is_water(nb)) {
            (void) world.set_block(np, is_fluid_source(nb) ? BLOCK_OBSIDIAN : BLOCK_COBBLESTONE);
            result = BLOCK_COBBLESTONE;
            continue;
        }
        if (nb == BLOCK_AIR) {
            int new_level = level + 1;
            if (new_level > max_level) continue;
            BlockId new_block = is_water_block ? water_for_level(new_level) : BLOCK_LAVA;
            (void) world.set_block(np, new_block);
            ticks.schedule(np, new_block, current_tick, WATER_FLOW_DELAY);
            if (result == BLOCK_AIR) result = new_block;
        }
    }
    return result;
}
} // namespace

void TickSystem::schedule(BlockPos pos, BlockId block, int64_t current_tick, int delay_ticks, int priority) {
    queue_.push(ScheduledTick{pos, block, current_tick + delay_ticks, priority});
}

void TickSystem::process(World& world, int64_t current_tick, Rng& rng) {
    int processed = 0;
    while (!queue_.empty() && processed < MAX_TICKS_PER_SERVER_TICK) {
        const ScheduledTick top = queue_.top();
        if (top.target_tick > current_tick) break;
        queue_.pop();
        ++processed;

        if (world.get_block(top.pos) != top.block) continue;

        switch (top.block) {
            case BLOCK_WATER: spread_fluid(*this, world, top.pos, BLOCK_WATER, rng, current_tick); break;
            case BLOCK_LAVA: spread_fluid(*this, world, top.pos, BLOCK_LAVA, rng, current_tick); break;
            default: break;
        }
    }
}

void TickSystem::process_fluids(World& world, int64_t current_tick, Rng& rng) {
    int processed = 0;
    // Only process sections that might have fluids (above bedrock, near surface).
    for (const auto& [cp, chunk] : world.chunks()) {
        if (processed >= MAX_TICKS_PER_SERVER_TICK) break;

        // Fluid-presence cache: 0=unknown, 1=no fluids found (skip), 2=armed
        // (worldgen chunk or edit since last scan — run a full scan),
        // 3=scanned, nothing changed since (skip). Chunk::set_block re-arms
        // edited chunks, so flow behavior matches a full re-scan while
        // cutting the steady-state cost to zero.
        uint8_t fc = chunk->fluid_cache.load(std::memory_order_relaxed);
        if (fc == 0) fc = 2; // unknown: do the full scan once, latch result
        if (fc != 2) continue;

        bool found_any = false;
        for (int sy = 0; sy < SECTIONS_PER_CHUNK; ++sy) {
            int wy = MIN_Y + sy * SECTION_SIZE;
            if (wy > SEA_LEVEL + 16) continue; // well above sea level
            if (wy + SECTION_SIZE < MIN_Y + 4) continue; // bedrock

            for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
                for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                    // Check surface water first (top-down, break after first fluid).
                    for (int ly = SECTION_SIZE - 1; ly >= 0; --ly) {
                        int wby = MIN_Y + sy * SECTION_SIZE + ly;
                        BlockPos p{cp.x * CHUNK_SIZE + lx, wby, cp.z * CHUNK_SIZE + lz};
                        BlockId b = chunk->sections[sy].get(lx, ly, lz);
                        if (!is_fluid(b)) continue;
                        if (b == BLOCK_WATER || b == BLOCK_LAVA) {
                            schedule(p, b, current_tick, WATER_FLOW_DELAY);
                        } else {
                            spread_fluid(*this, world, p, b, rng, current_tick);
                        }
                        found_any = true;
                        ++processed;
                        break; // only top fluid per column
                    }
                }
            }
        }
        // Scanned: go quiet until the next edit in this chunk re-arms us.
        chunk->fluid_cache.store(found_any ? 3 : 1, std::memory_order_relaxed);
    }
}

void TickSystem::random_ticks(World& world, Rng& rng) {
    for (const auto& [cp, chunk] : world.chunks()) {
        for (int s = 0; s < SECTIONS_PER_CHUNK; ++s) {
            if (chunk->sections[s].is_all_air()) continue;

            for (int t = 0; t < RANDOM_TICK_SPEED; ++t) {
                int lx = rng.next_int(CHUNK_SIZE);
                int ly = rng.next_int(SECTION_SIZE);
                int lz = rng.next_int(CHUNK_SIZE);
                int wy = MIN_Y + s * SECTION_SIZE + ly;
                BlockPos p{cp.x * CHUNK_SIZE + lx, wy, cp.z * CHUNK_SIZE + lz};
                BlockId b = world.get_block(p);

                if (b == BLOCK_GRASS) {
                    int dir = rng.next_int(6);
                    BlockPos np = p + DIRECTION_OFFSETS[dir];
                    if (world.get_block(np) == BLOCK_DIRT) {
                        BlockPos above{np.x, np.y + 1, np.z};
                        BlockId ab = world.get_block(above);
                        if (ab == BLOCK_AIR) (void) world.set_block(np, BLOCK_GRASS);
                    }
                } else if (b == BLOCK_SNOW || b == BLOCK_ICE) {
                    int sy = section_index(p.y);
                    uint8_t bl = chunk->light[sy].get_block_light(lx, local_y(p.y), lz);
                    uint8_t sl = chunk->light[sy].get_sky_light(lx, local_y(p.y), lz);
                    if (bl >= 12 || sl >= 12) {
                        (void) world.set_block(p, (b == BLOCK_ICE) ? BLOCK_WATER : BLOCK_AIR);
                    }
                }
            }
        }
    }
}

} // namespace mc

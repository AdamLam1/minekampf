#pragma once

#include <atomic>
#include <cstdint>

#include "world/chunk.hpp"
#include "world/world.hpp"

namespace mc {

// Cumulative light-engine statistics for perf tooling (automation get_perf).
// Function-local static keeps initialization order safe across TU boundaries.
struct LightStats {
    std::atomic<uint64_t> computes{0};
    std::atomic<uint64_t> total_us{0};
    void reset() {
        computes.store(0, std::memory_order_relaxed);
        total_us.store(0, std::memory_order_relaxed);
    }
};
[[nodiscard]] LightStats& light_stats();

// Per-chunk light calculation (PHASE3 §4.1).
// - Sky light: top-down per column (15 above terrain, 0 under opaque blocks),
//   then BFS flood-fill through non-opaque blocks so caves near entrances
//   receive ambient sky light bleed.
// - Block light: BFS flood-fill from emissive blocks within the chunk.
//
// Deviation note: cross-chunk light propagation is limited; light is computed
// per chunk but reads neighbor chunks during propagation when available.
// `cross_chunk_writes=false` keeps propagation inside `chunk` (used for
// worker-thread light before a chunk is shared with the main thread).
void compute_light(Chunk& chunk, const World& world, bool cross_chunk_writes = true);

} // namespace mc

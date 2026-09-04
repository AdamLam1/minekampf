#include "world/lighting.hpp"

#include <cstddef>
#include <chrono>
#include <vector>

#include "core/config.hpp"
#include "world/block.hpp"

namespace mc {

LightStats& light_stats() {
    static LightStats stats;
    return stats;
}

namespace {
struct LightNode {
    int16_t x;
    int16_t y; // world Y
    int16_t z;
    uint8_t level;
};

// Flat index over the whole chunk column for BFS scratch (16x256x16).
inline int col_index(int lx, int wy, int lz) { return ((wy - MIN_Y) * CHUNK_SIZE + lz) * CHUNK_SIZE + lx; }

// FIFO over a caller-owned vector: `head` advances instead of erasing from
// the front, so the BFS is allocation-free after warm-up.
void propagate_light(Chunk& chunk, const World& world, std::vector<LightNode>& q, size_t head, bool sky,
                     bool cross_chunk_writes) {
    while (head < q.size()) {
        LightNode n = q[head++];
        if (n.level <= 1) continue;
        uint8_t new_level = static_cast<uint8_t>(n.level - 1);
        for (int d = 0; d < 6; ++d) {
            BlockPos np(n.x + DIRECTION_OFFSETS[d].x, n.y + DIRECTION_OFFSETS[d].y, n.z + DIRECTION_OFFSETS[d].z);
            if (np.y < MIN_Y || np.y >= MAX_Y) continue;
            int lx = local_x(np.x);
            int lz = local_z(np.z);
            Chunk* c = &chunk;
            bool cross_chunk = (np.x >> 4) != chunk.pos.x || (np.z >> 4) != chunk.pos.z;
            if (cross_chunk) {
                if (!cross_chunk_writes) continue;
                c = world.get_chunk(chunk_from_block(np));
                if (!c) continue;
            }
            BlockId nb = c->get_block(np);
            if (nb != BLOCK_AIR && is_opaque(nb)) continue;
            int sy = section_index(np.y);
            int idx = section_index_3d(lx, local_y(np.y), lz);
            auto& arr = sky ? c->light[sy].sky_light : c->light[sy].block_light;
            uint8_t cur = LightData::get_nibble(arr, idx);
            if (new_level > cur) {
                LightData::set_nibble(arr, idx, new_level);
                if (new_level > 1) q.push_back({static_cast<int16_t>(np.x), static_cast<int16_t>(np.y), static_cast<int16_t>(np.z), new_level});
                if (cross_chunk) c->dirty.store(true, std::memory_order_relaxed);
            }
        }
    }
}
} // namespace

void compute_light(Chunk& chunk, const World& world, bool cross_chunk_writes) {
    const auto start = std::chrono::steady_clock::now();
    // Reset light. Sky light starts at 15 everywhere: the top-down pass below
    // only writes cells that drop below 15, so open-sky regions cost no
    // nibble writes at all.
    for (int s = 0; s < SECTIONS_PER_CHUNK; ++s) {
        chunk.light[s].block_light.fill(0);
        chunk.light[s].sky_light.fill(0xFF);
    }

    // --- Sky light: top-down per column ---
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            uint8_t sky = 15;
            int y = MAX_Y - 1;
            while (y >= MIN_Y) {
                // Whole air sections pass 15 straight through: jump past them
                // (values are already correct from the prefill).
                if (sky == 15) {
                    int sy = section_index(y);
                    if (chunk.sections[sy].is_all_air()) {
                        y = MIN_Y + sy * SECTION_SIZE - 1;
                        continue;
                    }
                }
                BlockId b = chunk.get_block(lx, y, lz);
                if (is_opaque(b)) {
                    sky = 0;
                } else if (opacity(b) > 0) {
                    sky = static_cast<uint8_t>(sky > opacity(b) ? sky - opacity(b) : 0);
                }
                if (sky != 15) {
                    chunk.light[section_index(y)].set_sky_light(lx, local_y(y), lz, sky);
                }
                --y;
            }
        }
    }

    // --- Sky light: BFS flood from every lit cell ---
    // The old 2-pass horizontal spread could not propagate light DOWNWARD
    // (only intra-section, same-ly neighbors), so cave chambers under a
    // surface opening kept black floors while their ceilings glowed. A proper
    // flood from all lit cells (including the pre-filled 15s at chamber
    // mouths) fixes vertical flow; the `new_level > cur` guard makes the BFS
    // a no-op where the column pass already settled everything.
    thread_local std::vector<LightNode> sky_q;
    sky_q.clear();
    size_t sky_head = 0;
    for (int sy = 0; sy < SECTIONS_PER_CHUNK; ++sy) {
        // Fully sky-lit section (pure prefill, no dark cell inside): its cells
        // have nothing to spread — every flow into darker areas starts from a
        // lit cell that lives in a non-uniform section.
        bool all_15 = true;
        for (uint8_t b : chunk.light[sy].sky_light) {
            if (b != 0xFF) { all_15 = false; break; }
        }
        if (all_15) continue;
        for (int idx = 0; idx < SECTION_VOLUME; ++idx) {
            uint8_t sky = LightData::get_nibble(chunk.light[sy].sky_light, idx);
            if (sky < 2) continue;
            int lx = idx & 15;
            int ly = (idx >> 8) & 15;
            int lz = (idx >> 4) & 15;
            int wy = MIN_Y + sy * SECTION_SIZE + ly;
            sky_q.push_back({static_cast<int16_t>(lx + chunk.pos.x * CHUNK_SIZE), static_cast<int16_t>(wy),
                             static_cast<int16_t>(lz + chunk.pos.z * CHUNK_SIZE), sky});
        }
    }
    propagate_light(chunk, world, sky_q, sky_head, true, cross_chunk_writes);

    // --- Block light: seed emissive sources then BFS ---
    // Vector-backed FIFO (head index) instead of std::queue: no per-node
    // heap allocations, and the buffer is reused across computes.
    thread_local std::vector<LightNode> light_q;
    light_q.clear();
    size_t head = 0;
    for (int sy = 0; sy < SECTIONS_PER_CHUNK; ++sy) {
        if (chunk.sections[sy].is_all_air()) continue; // no emissive blocks there
        for (int idx = 0; idx < SECTION_VOLUME; ++idx) {
            BlockId b = chunk.sections[sy].get_linear(idx);
            uint8_t em = light_emission(b);
            if (em == 0) continue;
            int lx = idx & 15;
            int ly = (idx >> 8) & 15;
            int lz = (idx >> 4) & 15;
            int wy = MIN_Y + sy * SECTION_SIZE + ly;
            chunk.light[sy].set_block_light(lx, ly, lz, em);
            light_q.push_back({static_cast<int16_t>(lx + chunk.pos.x * CHUNK_SIZE), static_cast<int16_t>(wy),
                    static_cast<int16_t>(lz + chunk.pos.z * CHUNK_SIZE), em});
        }
    }
    propagate_light(chunk, world, light_q, head, false, cross_chunk_writes);

    chunk.light_dirty.store(false, std::memory_order_relaxed);

    auto& st = light_stats();
    st.computes.fetch_add(1, std::memory_order_relaxed);
    st.total_us.fetch_add(
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start)
                .count()),
        std::memory_order_relaxed);
}

} // namespace mc

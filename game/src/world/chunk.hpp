#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "core/config.hpp"
#include "core/types.hpp"
#include "generation/biomes.hpp"
#include "world/block.hpp"
#include "world/paletted_container.hpp"

namespace mc {

// Light storage for one section (PHASE2 §3.3): 4 bits per block, packed two
// per byte. Block light + sky light, each 2048 bytes.
struct LightData {
    std::array<uint8_t, SECTION_VOLUME / 2> block_light{}; // 2048 B
    std::array<uint8_t, SECTION_VOLUME / 2> sky_light{};   // 2048 B

    [[nodiscard]] static uint8_t get_nibble(const std::array<uint8_t, SECTION_VOLUME / 2>& arr, int index) {
        int byte_index = index >> 1;
        uint8_t b = arr[byte_index];
        return (index & 1) ? static_cast<uint8_t>((b >> 4) & 0xF) : static_cast<uint8_t>(b & 0xF);
    }
    static void set_nibble(std::array<uint8_t, SECTION_VOLUME / 2>& arr, int index, uint8_t value) {
        int byte_index = index >> 1;
        uint8_t b = arr[byte_index];
        if (index & 1) {
            b = static_cast<uint8_t>((b & 0x0F) | ((value & 0xF) << 4));
        } else {
            b = static_cast<uint8_t>((b & 0xF0) | (value & 0xF));
        }
        arr[byte_index] = b;
    }

    [[nodiscard]] uint8_t get_block_light(int x, int y, int z) const { return get_nibble(block_light, section_index_3d(x, y, z)); }
    [[nodiscard]] uint8_t get_sky_light(int x, int y, int z) const { return get_nibble(sky_light, section_index_3d(x, y, z)); }
    void set_block_light(int x, int y, int z, uint8_t v) { set_nibble(block_light, section_index_3d(x, y, z), v); }
    void set_sky_light(int x, int y, int z, uint8_t v) { set_nibble(sky_light, section_index_3d(x, y, z), v); }
};

// Chunk generation state machine (PHASE1 §3.3).
enum class ChunkStatus : uint8_t {
    Empty,
    Biomes,
    Noise,
    Surface,
    Carvers,
    Liquids,
    Features,
    Light,
    Full,
};

// A chunk column: 16x16x WORLD_HEIGHT blocks, stored as SECTIONS_PER_CHUNK
// sections (PHASE2 architecture).
struct Chunk {
    ChunkPos pos;
    std::array<PalettedContainer, SECTIONS_PER_CHUNK> sections;
    std::array<LightData, SECTIONS_PER_CHUNK> light;
    std::array<int, CHUNK_SIZE * CHUNK_SIZE> heightmap{}; // highest non-air Y per column
    // Biome id (Biome enum) per column, filled by the generator. Only the
    // Overworld stores real biomes; other dimensions leave zeros (Ocean).
    std::array<uint8_t, CHUNK_SIZE * CHUNK_SIZE> biomes{};

    [[nodiscard]] Biome biome_at(int lx, int lz) const {
        return static_cast<Biome>(biomes[lz * CHUNK_SIZE + lx]);
    }
    std::atomic<ChunkStatus> status{ChunkStatus::Empty};
    std::atomic<bool> dirty{true};          // mesh needs rebuild
    std::atomic<bool> light_dirty{true};    // light needs recompute
    std::atomic<bool> save_dirty{true};     // needs save to disk
    std::atomic<int> ref_count{0};          // for unload decisions
    // 0 = unknown (needs probe), 1 = probed no fluids, 2 = fluid edit since
    // last scan (needs activation scan), 3 = scanned and stable. The fluid
    // tick skips chunks in states 1 and 3 instead of rescanning the volume
    // every 4 ticks; any set_block touching a fluid re-arms the scan.
    std::atomic<uint8_t> fluid_cache{0};

    Chunk() = default;
    explicit Chunk(ChunkPos p) {
        reset(p);
    }

    void reset(ChunkPos p) {
        pos = p;
        for (auto& sec : sections) sec.fill(BLOCK_AIR);
        for (auto& l : light) {
            l.block_light.fill(0);
            l.sky_light.fill(0);
        }
        heightmap.fill(0);
        status.store(ChunkStatus::Empty, std::memory_order_relaxed);
        dirty.store(true, std::memory_order_relaxed);
        light_dirty.store(true, std::memory_order_relaxed);
        save_dirty.store(true, std::memory_order_relaxed);
        ref_count.store(0, std::memory_order_relaxed);
        fluid_cache.store(0, std::memory_order_relaxed);
    }

    // Block access. `y` is a world Y in [MIN_Y, MAX_Y).
    [[nodiscard]] BlockId get_block(int lx, int wy, int lz) const {
        if (wy < MIN_Y || wy >= MAX_Y) return BLOCK_AIR;
        int sy = section_index(wy);
        return sections[sy].get(lx, local_y(wy), lz);
    }
    [[nodiscard]] BlockId get_block(BlockPos p) const {
        return get_block(local_x(p.x), p.y, local_z(p.z));
    }

    void set_block(int lx, int wy, int lz, BlockId b) {
        if (wy < MIN_Y || wy >= MAX_Y) return;
        int sy = section_index(wy);
        BlockId old = sections[sy].get(lx, local_y(wy), lz);
        sections[sy].set(lx, local_y(wy), lz, b);
        dirty.store(true, std::memory_order_relaxed);
        light_dirty.store(true, std::memory_order_relaxed);
        save_dirty.store(true, std::memory_order_relaxed);
        // Keep the fluid cache honest: fluid edits re-arm the activation scan,
        // and ANY edit in a chunk that may contain fluids re-arms it too (a
        // newly opened hole can let neighbouring water start flowing).
        if (is_fluid(b) || is_fluid(old) || fluid_cache.load(std::memory_order_relaxed) != 1) {
            fluid_cache.store(2, std::memory_order_relaxed);
        }
        // Update heightmap (highest non-fluid block per column).
        int col = lz * CHUNK_SIZE + lx;
        bool is_fluid_block = is_fluid(b);
        if (b != BLOCK_AIR && !is_fluid_block && wy >= heightmap[col]) {
            heightmap[col] = wy + 1;
        } else if ((b == BLOCK_AIR || is_fluid_block) && heightmap[col] == wy + 1) {
            // Recompute downward (cheap, only when top removed).
            int h = MIN_Y;
            for (int yy = MAX_Y - 1; yy >= MIN_Y; --yy) {
                if (get_block(lx, yy, lz) != BLOCK_AIR) { h = yy + 1; break; }
            }
            heightmap[col] = h;
        }
    }
    void set_block(BlockPos p, BlockId b) { set_block(local_x(p.x), p.y, local_z(p.z), b); }

    [[nodiscard]] uint8_t get_block_light(BlockPos p) const {
        if (p.y < MIN_Y || p.y >= MAX_Y) return 0;
        int sy = section_index(p.y);
        return light[sy].get_block_light(local_x(p.x), local_y(p.y), local_z(p.z));
    }
    [[nodiscard]] uint8_t get_block_light(int lx, int wy, int lz) const {
        if (wy < MIN_Y || wy >= MAX_Y) return 0;
        int sy = section_index(wy);
        return light[sy].get_block_light(lx, local_y(wy), lz);
    }

    void fill_section(int sy, BlockId b) {
        if (sy < 0 || sy >= SECTIONS_PER_CHUNK) return;
        sections[sy].fill(b);
    }

    void mark_dirty() { dirty.store(true, std::memory_order_relaxed); }
    [[nodiscard]] bool is_dirty() const { return dirty.load(std::memory_order_relaxed); }
};

} // namespace mc

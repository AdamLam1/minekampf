#pragma once

#include <cstdint>
#include <vector>

#include "core/types.hpp"
#include "world/block.hpp"

namespace mc {

// Vertex layout (28 bytes). Simpler than the plan's 20-byte packed format
// (PHASE3 §3.2) — chosen for correctness first; can be tightened in PHASE14.
//   offset 0:  vec3 position (chunk-local)
//   offset 12: vec3 atlas UVW
//   offset 24: u8x4 (block_light, sky_light, ao, face)
//   offset 28: u8x4 (r, g, b, a) tint + alpha
struct Vertex {
    float x, y, z;
    float u, v, w;
    uint8_t bl, sl, ao, face;
    uint8_t r, g, b, a;
};
static_assert(sizeof(Vertex) == 32, "Vertex must be 32 bytes");

// CPU-side mesh built by a worker thread (PHASE1 §3.2 work result).
struct ChunkMeshData {
    std::vector<Vertex> opaque_verts;
    std::vector<uint32_t> opaque_idx;
    std::vector<Vertex> trans_verts;
    std::vector<uint32_t> trans_idx;
    ChunkPos pos;
};

// GPU-side vertex, Sodium-style compact layout (24 bytes vs 32 of Vertex).
// Positions are whole-block WORLD coordinates (range fits i16 comfortably),
// stored as 3x int16 — lossless. UVs span [0,16] tile units (greedy repeats)
// and are quantized to u16 each — 4x finer than one texel. The rest is the
// old metadata (tile, lights, face, tint) as bytes.
struct PackedVertex {
    int16_t x, y, z;
    uint16_t pad_pos;
    uint32_t uv;    // u(16) v(16), quantized uv/16*65535
    uint8_t tile, bl, sl, ao;
    uint8_t face, r, g, b;
    uint8_t a, pad0, pad1, pad2;
};
static_assert(sizeof(PackedVertex) == 24, "PackedVertex must be 24 bytes");

// GPU-side mesh for one chunk (opaque + transparent passes).
struct GpuMesh {
    uint32_t vao = 0;
    uint32_t vbo = 0;
    uint32_t ibo = 0;
    int index_count = 0;
    bool valid = false;

    void create();
    void destroy();
    void upload(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx);
    void draw() const;
};

struct ChunkMesh {
    GpuMesh opaque;
    GpuMesh transparent;
    bool uploaded = false;
};

} // namespace mc

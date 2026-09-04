#pragma once

#include "core/types.hpp"
#include "renderer/chunk_mesh.hpp"
#include "renderer/texture_atlas.hpp"
#include "world/world.hpp"
#include <array>
#include <memory>

namespace mc {

// Build a chunk mesh on a worker thread (PHASE1 §3.2, PHASE3 §1-2).
// Face culling + per-face quad emission with ambient occlusion (PHASE3 §4.3).
//
// Reads blocks via `world` (handles cross-chunk neighbors for culling/AO).
// Reads light from the chunk's light arrays (must be computed beforehand).
// Pure read pass - fills out_data using object pooling (Golden Rule #4).
void build_chunk_mesh(const std::array<std::shared_ptr<Chunk>, 9>& chunks, ChunkPos pos, const TextureAtlas& atlas, ChunkMeshData& out_data);

} // namespace mc

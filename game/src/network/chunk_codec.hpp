#pragma once

#include <cstdint>
#include <vector>

namespace mc {

struct Chunk;

// Wire codec for chunk streaming: sections serialize as their native palette
// + packed words (compact for typical terrain), plus heightmap and biomes
// (the save format's NBT drops biomes; the renderer's foliage tint needs
// them). Payloads are zlib-compressed whole.
//
// Layout (big-endian via PacketBuffer semantics, hand-rolled here to avoid
// pulling net types into world/):
//   varint version (2)
//   256 x varint heightmap
//   256 bytes biomes
//   for each of the 16 sections:
//     u8 present
//     if present: varint palette size, palette block ids (varints),
//                 u8 bits, varint word count, words (u64 little-endian)
[[nodiscard]] std::vector<uint8_t> serialize_chunk(const Chunk& chunk);
// Restores into `chunk` (must already be reset to the right ChunkPos).
// Returns false on malformed input; `chunk` may be partially modified then.
[[nodiscard]] bool deserialize_chunk(const std::vector<uint8_t>& payload, Chunk& chunk);

[[nodiscard]] std::vector<uint8_t> zlib_compress(const std::vector<uint8_t>& in);
[[nodiscard]] bool zlib_decompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);

} // namespace mc

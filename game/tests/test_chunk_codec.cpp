#include <gtest/gtest.h>
#include "network/chunk_codec.hpp"
#include "world/chunk.hpp"

namespace mc {

TEST(ChunkCodec, RoundTripPreservesBlocksHeightmapBiomes) {
    Chunk original{ChunkPos{3, -4}};
    // Palette-heavy section (y=4, world Y 64..79) with several block types.
    const BlockId palette[] = {BLOCK_AIR, BLOCK_STONE, BLOCK_DIRT, BLOCK_GRASS,
                               BLOCK_OAK_LOG, BLOCK_OAK_LEAVES, BLOCK_WATER};
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                original.sections[4].set(x, y, z, palette[(x + z + y) % 7]);
            }
        }
    }
    // Single-state section (solid stone at y=0) and all-air sections in
    // between must all survive.
    original.sections[0].fill(BLOCK_STONE);
    original.set_block(5, 65, 5, BLOCK_GLOWSTONE); // palette grows
    for (int i = 0; i < 256; ++i) {
        original.heightmap[i] = 64 + (i % 7);
        original.biomes[i] = static_cast<uint8_t>((i * 31) % 12);
    }

    auto compressed = zlib_compress(serialize_chunk(original));
    ASSERT_FALSE(compressed.empty());
    ASSERT_GT(compressed.size(), 0u);

    Chunk restored{ChunkPos{3, -4}};
    auto raw = std::vector<uint8_t>{};
    ASSERT_TRUE(zlib_decompress(compressed, raw));
    ASSERT_TRUE(deserialize_chunk(raw, restored));

    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                EXPECT_EQ(restored.sections[4].get(x, y, z),
                          original.sections[4].get(x, y, z))
                    << "at section-local " << x << "," << y << "," << z;
            }
        }
    }
    EXPECT_EQ(restored.sections[0].single_state(), BLOCK_STONE);
    for (int y = 1; y < SECTIONS_PER_CHUNK; ++y) {
        if (y == 4) continue;
        EXPECT_TRUE(restored.sections[y].is_all_air()) << "section " << y;
    }
    EXPECT_EQ(restored.heightmap, original.heightmap);
    EXPECT_EQ(restored.biomes, original.biomes);
    EXPECT_EQ(restored.get_block(5, 65, 5), BLOCK_GLOWSTONE);
}

TEST(ChunkCodec, EmptyChunkRoundTrips) {
    Chunk original{ChunkPos{0, 0}};
    auto compressed = zlib_compress(serialize_chunk(original));
    ASSERT_FALSE(compressed.empty());
    Chunk restored{ChunkPos{0, 0}};
    std::vector<uint8_t> raw;
    ASSERT_TRUE(zlib_decompress(compressed, raw));
    ASSERT_TRUE(deserialize_chunk(raw, restored));
    for (auto& sec : restored.sections) {
        ASSERT_TRUE(sec.is_all_air());
    }
}

TEST(ChunkCodec, RejectsCorruptAndForeignPayloads) {
    Chunk chunk{ChunkPos{0, 0}};

    EXPECT_FALSE(deserialize_chunk({}, chunk));
    EXPECT_FALSE(deserialize_chunk({0x01}, chunk)); // truncated version varint

    std::vector<uint8_t> bad_version = {0x03}; // version 3, then EOF
    EXPECT_FALSE(deserialize_chunk(bad_version, chunk));

    // Right version but truncated heightmap.
    std::vector<uint8_t> truncated = {0x02, 0x00, 0x01, 0x02};
    EXPECT_FALSE(deserialize_chunk(truncated, chunk));

    // Garbage that is not a valid zlib stream must fail decompression.
    std::vector<uint8_t> garbage(64, 0xAB);
    std::vector<uint8_t> out;
    EXPECT_FALSE(zlib_decompress(garbage, out));
}

TEST(ChunkCodec, CompressionShrinksTypicalTerrain) {
    // A natural-ish chunk (stone below 60, air above) must shrink: TCP without
    // transport compression is the MVP carrier.
    Chunk chunk{ChunkPos{-7, 9}};
    for (int y = 0; y < 60; ++y) chunk.sections[y / 16].set_linear(0, BLOCK_STONE);
    auto compressed = zlib_compress(serialize_chunk(chunk));
    ASSERT_FALSE(compressed.empty());
    EXPECT_LT(compressed.size(), 64u * 1024u);
}

} // namespace mc

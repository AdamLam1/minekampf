#include <gtest/gtest.h>
#include "network/connection.hpp"
#include "network/buffer.hpp"

#include <zlib.h>

#include <cstring>

namespace mc::net {

namespace {

// Mirrors Connection::send_packet's wire format.
std::vector<uint8_t> encode_frame(int32_t id, const std::vector<uint8_t>& body,
                                  int compression_threshold) {
    PacketBuffer body_buf(body);
    PacketBuffer frame;

    if (compression_threshold >= 0) {
        PacketBuffer payload;
        if (static_cast<int>(body.size()) < compression_threshold) {
            payload.write_varint(0); // stored uncompressed
            const auto& raw = body_buf.data();
            payload.write_bytes(std::span<const uint8_t>(raw.data(), raw.size()));
        } else {
            std::vector<uint8_t> compressed(compressBound(static_cast<uLong>(body.size())));
            uLongf len = compressed.size();
            EXPECT_EQ(compress(compressed.data(), &len, body.data(),
                               static_cast<uLong>(body.size())), Z_OK);
            payload.write_varint(static_cast<int32_t>(body.size()));
            payload.write_bytes(std::span<const uint8_t>(compressed.data(), len));
        }
        frame.write_varint(static_cast<int32_t>(payload.data().size()) +
                           PacketBuffer::varint_size(id));
        frame.write_varint(id);
        const auto& p = payload.data();
        frame.write_bytes(std::span<const uint8_t>(p.data(), p.size()));
    } else {
        const auto& raw = body_buf.data();
        frame.write_varint(static_cast<int32_t>(raw.size()) + PacketBuffer::varint_size(id));
        frame.write_varint(id);
        frame.write_bytes(std::span<const uint8_t>(raw.data(), raw.size()));
    }
    return frame.data();
}

// On the wire the varint length prefix is consumed by do_read_header();
// handle_packet() only ever sees the bytes after it.
std::vector<uint8_t> strip_length(const std::vector<uint8_t>& full_frame) {
    PacketBuffer tmp(full_frame);
    (void)tmp.read_varint();
    return tmp.read_bytes(tmp.readable_bytes());
}

} // namespace

TEST(NetworkFramesTest, UncompressedRoundTripPreservesIdAndBody) {
    const int32_t id = 0x25;
    const std::vector<uint8_t> body = {0xDE, 0xAD, 0xBE, 0xEF};

    auto frame = strip_length(encode_frame(id, body, -1));
    auto parsed = decode_frame(frame, -1);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->first, id);
    EXPECT_EQ(parsed->second.read_bytes(body.size()), body);
}

TEST(NetworkFramesTest, CompressedRoundTripDoesNotEatBodyByteAsId) {
    // Regression: the old decoder re-read a VarInt id from inside the
    // decompressed payload, corrupting every compressed packet.
    const int32_t id = 0x44;
    const std::vector<uint8_t> body = {0x01, 0x02, 0x03, 0x04, 0x05,
                                       0x06, 0x07, 0x08, 0x09, 0x0A};

    auto frame = strip_length(encode_frame(id, body, /*threshold=*/4)); // forces compression
    auto parsed = decode_frame(frame, /*threshold=*/4);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->first, id) << "packet id must survive compression";
    EXPECT_EQ(parsed->second.read_bytes(body.size()), body)
        << "decompressed body must start with real payload bytes";
}

TEST(NetworkFramesTest, BelowThresholdStaysUncompressedWithZeroDataLength) {
    const int32_t id = 0x21;
    const std::vector<uint8_t> body = {0xAA};

    auto frame = strip_length(encode_frame(id, body, /*threshold=*/64));
    auto parsed = decode_frame(frame, 64);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->first, id);
    EXPECT_EQ(parsed->second.read_bytes(body.size()), body);
}

TEST(NetworkFramesTest, CorruptCompressedStreamIsRejected) {
    // Correct wire shape: [len][id][data_length][garbage "compressed" bytes].
    const int32_t id = 0x25;
    std::vector<uint8_t> garbage = {1, 2, 3, 4, 5, 6, 7, 8};

    PacketBuffer payload;
    payload.write_varint(100); // claims a 100-byte inflated body
    payload.write_bytes(std::span<const uint8_t>(garbage.data(), garbage.size()));

    PacketBuffer frame;
    frame.write_varint(static_cast<int32_t>(payload.data().size()) +
                       PacketBuffer::varint_size(id));
    frame.write_varint(id);
    const auto& p = payload.data();
    frame.write_bytes(std::span<const uint8_t>(p.data(), p.size()));

    auto parsed = decode_frame(strip_length(frame.data()), 4);
    EXPECT_FALSE(parsed.has_value());
}

TEST(NetworkFramesTest, TruncatedVarintIsRejected) {
    std::vector<uint8_t> bad = {0xFF}; // continuation bit without terminator
    auto parsed = decode_frame(bad, -1);
    EXPECT_FALSE(parsed.has_value());
}

} // namespace mc::net

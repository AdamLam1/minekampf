#include "network/chunk_codec.hpp"

#include "world/chunk.hpp"

#include <zlib.h>

#include <cstring>
#include <stdexcept>

namespace mc {

namespace {

constexpr int32_t kCodecVersion = 2;

// Minimal big-endian byte sink/source, independent of net::PacketBuffer so
// the codec can be unit-tested without sockets and shared by both sides.
class ByteWriter {
public:
    void write_byte(uint8_t b) { out_.push_back(b); }
    void write_u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            out_.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF)); // little-endian words
        }
    }
    void write_varint(int32_t value) {
        uint32_t u = static_cast<uint32_t>(value);
        while (true) {
            if ((u & ~0x7Fu) == 0) {
                write_byte(static_cast<uint8_t>(u));
                return;
            }
            write_byte(static_cast<uint8_t>((u & 0x7F) | 0x80));
            u >>= 7;
        }
    }
    void write_raw(const uint8_t* data, size_t len) { out_.insert(out_.end(), data, data + len); }
    [[nodiscard]] std::vector<uint8_t> take() { return std::move(out_); }

private:
    std::vector<uint8_t> out_;
};

class ByteReader {
public:
    explicit ByteReader(const std::vector<uint8_t>& in) : in_(in) {}

    uint8_t read_byte() {
        if (pos_ >= in_.size()) throw std::out_of_range("chunk codec: end of stream");
        return in_[pos_++];
    }
    uint64_t read_u64() {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(read_byte()) << (i * 8);
        }
        return v;
    }
    int32_t read_varint() {
        uint32_t result = 0;
        int shift = 0;
        while (true) {
            uint8_t b = read_byte();
            result |= static_cast<uint32_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
            if (shift >= 32) throw std::runtime_error("chunk codec: varint too big");
        }
        return static_cast<int32_t>(result);
    }
    void read_raw(uint8_t* dst, size_t len) {
        if (pos_ + len > in_.size()) throw std::out_of_range("chunk codec: end of stream");
        std::memcpy(dst, in_.data() + pos_, len);
        pos_ += len;
    }

private:
    const std::vector<uint8_t>& in_;
    size_t pos_ = 0;
};

} // namespace

std::vector<uint8_t> serialize_chunk(const Chunk& chunk) {
    ByteWriter w;
    w.write_varint(kCodecVersion);
    for (int h : chunk.heightmap) w.write_varint(h);
    w.write_raw(chunk.biomes.data(), chunk.biomes.size());

    for (int sy = 0; sy < SECTIONS_PER_CHUNK; ++sy) {
        const PalettedContainer& sec = chunk.sections[sy];
        if (sec.is_all_air()) {
            w.write_byte(0);
            continue;
        }
        w.write_byte(1);
        const auto& palette = sec.palette();
        w.write_varint(static_cast<int32_t>(palette.size()));
        for (BlockId b : palette) w.write_varint(static_cast<int32_t>(b));
        w.write_byte(static_cast<uint8_t>(sec.bits()));
        const auto& words = sec.packed_data();
        w.write_varint(static_cast<int32_t>(words.size()));
        for (uint64_t word : words) w.write_u64(word);
    }
    return w.take();
}

bool deserialize_chunk(const std::vector<uint8_t>& payload, Chunk& chunk) {
    try {
        ByteReader r(payload);
        int32_t version = r.read_varint();
        if (version != kCodecVersion) return false;

        for (int& h : chunk.heightmap) h = r.read_varint();
        r.read_raw(chunk.biomes.data(), chunk.biomes.size());

        for (int sy = 0; sy < SECTIONS_PER_CHUNK; ++sy) {
            uint8_t present = r.read_byte();
            if (!present) {
                if (!chunk.sections[sy].is_all_air()) chunk.sections[sy].fill(BLOCK_AIR);
                continue;
            }
            int32_t palette_size = r.read_varint();
            if (palette_size <= 0 || palette_size > 256) return false;
            std::vector<BlockId> palette;
            palette.reserve(static_cast<size_t>(palette_size));
            for (int32_t i = 0; i < palette_size; ++i) {
                int32_t b = r.read_varint();
                if (b < 0 || b >= BLOCK_COUNT) return false;
                palette.push_back(static_cast<BlockId>(b));
            }
            int bits = r.read_byte();
            if (bits < 4 || bits > 8) return false;
            int32_t word_count = r.read_varint();
            int expected = (SECTION_VOLUME * bits + 63) / 64;
            if (word_count != expected) return false;
            std::vector<uint64_t> words;
            words.reserve(static_cast<size_t>(word_count));
            for (int32_t i = 0; i < word_count; ++i) words.push_back(r.read_u64());
            chunk.sections[sy].load_packed(bits, std::move(palette), std::move(words));
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<uint8_t> zlib_compress(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out(compressBound(static_cast<uLong>(in.size())));
    uLongf out_len = out.size();
    if (compress(out.data(), &out_len, in.data(), static_cast<uLong>(in.size())) != Z_OK) {
        return {}; // caller treats empty as "chunk not sendable"
    }
    out.resize(out_len);
    return out;
}

bool zlib_decompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    // The wire never stores raw (see zlib_compress), so require a plausible
    // inflated size up front: the codec's worst case is 16 sections with a
    // 256-entry palette + packed words + header, far below 8 MiB.
    uLongf out_len = 8u << 20;
    out.resize(out_len);
    if (uncompress(out.data(), &out_len, in.data(), static_cast<uLong>(in.size())) != Z_OK) {
        return false;
    }
    out.resize(out_len);
    return true;
}

} // namespace mc

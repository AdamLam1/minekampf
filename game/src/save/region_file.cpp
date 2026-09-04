#include "save/region_file.hpp"
#include "save/safe_file.hpp"
#include <zlib.h>

#include <chrono>
#include <cstring>
#include <bit>
#include <array>
#include <filesystem>

namespace mc {

namespace fs = std::filesystem;

// Endianness swap for header
static uint32_t swap_endian32(uint32_t u) {
    if constexpr (std::endian::native == std::endian::big) return u;
    auto u8 = std::bit_cast<std::array<uint8_t, 4>>(u);
    std::array<uint8_t, 4> res = { u8[3], u8[2], u8[1], u8[0] };
    return std::bit_cast<uint32_t>(res);
}

RegionFile::RegionFile(const std::string& filepath)
    : m_filepath(filepath), m_locations(1024), m_timestamps(1024) {
    fs::path p(filepath);
    if (!p.parent_path().empty() && !fs::exists(p.parent_path())) {
        std::error_code mk_ec;
        fs::create_directories(p.parent_path(), mk_ec);
    }

    load_image();
    read_header();
}

RegionFile::~RegionFile() {
    // Nothing to do: writes are flushed eagerly by write_chunk.
}

void RegionFile::load_image() {
    // Main file first; on truncation/corruption fall back to the .bak copy
    // that write_atomic rotated aside. A region header is at least 8KB.
    std::vector<uint8_t> loaded;
    if (savefs::read_with_fallback_validated(
            m_filepath, loaded,
            [](const std::vector<uint8_t>& b) { return b.size() >= 8192; })) {
        m_image = std::move(loaded);
        m_image.resize(((m_image.size() + 4095) / 4096) * 4096, 0);
        return;
    }

    // Fresh region: 8KB zeroed header.
    m_image.assign(8192, 0);
    m_dirty_image = true;
    flush_image();
}

void RegionFile::read_header() {
    if (m_image.size() < 8192) return;

    size_t total_sectors = m_image.size() / 4096;
    m_sector_free.assign(total_sectors, true);
    m_sector_free[0] = false; // Sectors 0 and 1 are header
    if (total_sectors > 1) m_sector_free[1] = false;

    // Read locations (4096 bytes)
    for (int i = 0; i < 1024; ++i) {
        const uint8_t* bytes = m_image.data() + i * 4;
        m_locations[i].offset = (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
        m_locations[i].count = bytes[3];

        if (m_locations[i].offset != 0 && m_locations[i].count != 0) {
            for (int s = 0; s < m_locations[i].count; ++s) {
                if (m_locations[i].offset + s < m_sector_free.size()) {
                    m_sector_free[m_locations[i].offset + s] = false;
                }
            }
        }
    }

    // Read timestamps (4096 bytes)
    for (int i = 0; i < 1024; ++i) {
        uint32_t ts;
        std::memcpy(&ts, m_image.data() + 4096 + i * 4, 4);
        m_timestamps[i] = swap_endian32(ts);
    }
}

void RegionFile::sync_header_bytes() {
    for (int i = 0; i < 1024; ++i) {
        uint32_t off = m_locations[i].offset;
        uint8_t* loc = m_image.data() + i * 4;
        loc[0] = static_cast<uint8_t>((off >> 16) & 0xFF);
        loc[1] = static_cast<uint8_t>((off >> 8) & 0xFF);
        loc[2] = static_cast<uint8_t>(off & 0xFF);
        loc[3] = m_locations[i].count;

        uint32_t ts = swap_endian32(m_timestamps[i]);
        std::memcpy(m_image.data() + 4096 + i * 4, &ts, 4);
    }
}

void RegionFile::flush_image() {
    if (!m_dirty_image) return;
    if (savefs::write_atomic(m_filepath, m_image.data(), m_image.size())) {
        m_dirty_image = false;
    }
    // On failure the image stays dirty; the next write retries. The previous
    // copy remains in .bak either way.
}

uint32_t RegionFile::allocate_sectors(uint8_t requested_sectors) {
    // Find contiguous free sectors
    int current_run = 0;
    int start_sector = -1;

    for (size_t i = 2; i < m_sector_free.size(); ++i) {
        if (m_sector_free[i]) {
            if (start_sector == -1) start_sector = (int)i;
            current_run++;
            if (current_run == requested_sectors) {
                for (int j = 0; j < requested_sectors; ++j) {
                    m_sector_free[start_sector + j] = false;
                }
                return start_sector;
            }
        } else {
            start_sector = -1;
            current_run = 0;
        }
    }

    // Not enough space, append to end
    uint32_t offset = (uint32_t)m_sector_free.size();
    for (int j = 0; j < requested_sectors; ++j) {
        m_sector_free.push_back(false);
    }
    return offset;
}

void RegionFile::free_sectors(uint32_t offset, uint8_t count) {
    for (int i = 0; i < count; ++i) {
        if (offset + i < m_sector_free.size()) {
            m_sector_free[offset + i] = true;
        }
    }
}

std::optional<NbtTag> RegionFile::read_chunk(int chunk_x, int chunk_z) {
    int local_x = chunk_x & 31;
    int local_z = chunk_z & 31;
    int index = local_x + local_z * 32;

    if (m_locations[index].offset == 0 || m_locations[index].count == 0) {
        return std::nullopt; // Chunk doesn't exist
    }

    size_t base = m_locations[index].offset * 4096;
    if (base + 5 > m_image.size()) return std::nullopt; // corrupted entry

    uint32_t length;
    std::memcpy(&length, m_image.data() + base, 4);
    length = swap_endian32(length);

    if (length == 0 || length > m_locations[index].count * 4096 ||
        base + 4 + length > m_image.size()) {
        return std::nullopt; // Corrupted
    }

    uint8_t compression_type = m_image[base + 4];

    std::vector<uint8_t> compressed_data(
        m_image.begin() + static_cast<long>(base) + 5,
        m_image.begin() + static_cast<long>(base) + 4 + length);

    std::vector<uint8_t> uncompressed;
    if (compression_type == 2) { // ZLIB
        uncompressed = decompress(compressed_data);
    } else {
        uncompressed = compressed_data; // Uncompressed/GZIP (not fully supported here for GZIP, but usually it's ZLIB)
    }

    if (uncompressed.empty()) return std::nullopt;

    auto [name, tag] = NbtSerializer::deserialize(uncompressed);
    return tag;
}

void RegionFile::write_chunk(int chunk_x, int chunk_z, const NbtTag& chunk_data) {
    int local_x = chunk_x & 31;
    int local_z = chunk_z & 31;
    int index = local_x + local_z * 32;

    std::vector<uint8_t> serialized = NbtSerializer::serialize("", chunk_data);
    std::vector<uint8_t> compressed = compress(serialized);

    uint32_t total_size = 5 + compressed.size(); // 4 bytes length, 1 byte compression type
    uint8_t sectors_needed = (total_size + 4095) / 4096;

    if (sectors_needed >= 256) {
        throw std::runtime_error("Chunk too large for region file format!");
    }

    uint32_t offset = m_locations[index].offset;
    uint8_t old_sectors = m_locations[index].count;

    if (offset != 0 && old_sectors >= sectors_needed) {
        // We can reuse the sectors. We might want to free the trailing unused ones, but MC often just keeps them allocated to the chunk
    } else {
        if (offset != 0) free_sectors(offset, old_sectors);
        offset = allocate_sectors(sectors_needed);
    }

    m_locations[index].offset = offset;
    m_locations[index].count = sectors_needed;
    m_timestamps[index] = (uint32_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Grow the image to cover the allocated sectors.
    size_t needed_bytes = (static_cast<size_t>(offset) + sectors_needed) * 4096;
    if (m_image.size() < needed_bytes) {
        m_image.resize(needed_bytes, 0);
        m_sector_free.resize(needed_bytes / 4096, true);
        // Re-apply allocations after the resize (vector<bool> copy of frees).
        m_sector_free[0] = false;
        if (m_sector_free.size() > 1) m_sector_free[1] = false;
        for (int i = 0; i < 1024; ++i) {
            for (int s = 0; s < m_locations[i].count; ++s) {
                if (m_locations[i].offset + s < m_sector_free.size()) {
                    m_sector_free[m_locations[i].offset + s] = false;
                }
            }
        }
    }

    // Write chunk data into the image.
    uint32_t written_len = swap_endian32((uint32_t)compressed.size() + 1);
    std::memcpy(m_image.data() + offset * 4096, &written_len, 4);
    m_image[offset * 4096 + 4] = 2; // ZLIB
    std::memcpy(m_image.data() + offset * 4096 + 5, compressed.data(), compressed.size());
    size_t remaining = (sectors_needed * 4096) - total_size;
    if (remaining > 0) {
        std::memset(m_image.data() + offset * 4096 + 5 + compressed.size(), 0, remaining);
    }

    sync_header_bytes();
    m_dirty_image = true;
    flush_image(); // atomic: tmp -> rotate .bak -> rename
}

std::vector<uint8_t> RegionFile::compress(const std::vector<uint8_t>& data) {
    z_stream defstream;
    defstream.zalloc = Z_NULL;
    defstream.zfree = Z_NULL;
    defstream.opaque = Z_NULL;

    defstream.avail_in = (uInt)data.size();
    defstream.next_in = (Bytef*)data.data();

    // deflater init.
    // 15 = max window bits, +0 = zlib header (not gzip which is +16)
    deflateInit(&defstream, Z_DEFAULT_COMPRESSION);

    std::vector<uint8_t> out;
    uint8_t temp_buffer[32768];

    do {
        defstream.avail_out = sizeof(temp_buffer);
        defstream.next_out = temp_buffer;
        deflate(&defstream, Z_FINISH);
        out.insert(out.end(), temp_buffer, temp_buffer + sizeof(temp_buffer) - defstream.avail_out);
    } while (defstream.avail_out == 0);

    deflateEnd(&defstream);
    return out;
}

std::vector<uint8_t> RegionFile::decompress(const std::vector<uint8_t>& data) {
    z_stream infstream;
    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;

    infstream.avail_in = (uInt)data.size();
    infstream.next_in = (Bytef*)data.data();

    inflateInit(&infstream);

    std::vector<uint8_t> out;
    uint8_t temp_buffer[32768];

    int ret;
    do {
        infstream.avail_out = sizeof(temp_buffer);
        infstream.next_out = temp_buffer;
        ret = inflate(&infstream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&infstream);
            return {}; // Error
        }
        out.insert(out.end(), temp_buffer, temp_buffer + sizeof(temp_buffer) - infstream.avail_out);
    } while (infstream.avail_out == 0);

    inflateEnd(&infstream);
    return out;
}

} // namespace mc

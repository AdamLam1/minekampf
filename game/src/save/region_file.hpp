#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "save/nbt.hpp"

namespace mc {

// Minecraft-style region file (8KB header, 4KB sectors, zlib chunks) kept
// crash-safe: the whole image lives in memory and every write_chunk flushes
// it through savefs::write_atomic (tmp -> rotate .bak -> rename). Loading
// falls back to the .bak copy when the main file is truncated or corrupted.
class RegionFile {
public:
    RegionFile(const std::string& filepath);
    ~RegionFile();

    // Prevent copy
    RegionFile(const RegionFile&) = delete;
    RegionFile& operator=(const RegionFile&) = delete;

    std::optional<NbtTag> read_chunk(int chunk_x, int chunk_z);
    void write_chunk(int chunk_x, int chunk_z, const NbtTag& chunk_data);

private:
    struct LocationEntry {
        uint32_t offset = 0; // In 4KB sectors
        uint8_t count = 0;   // In 4KB sectors
    };

    std::string m_filepath;
    std::vector<uint8_t> m_image; // full file image: header + sector data
    std::vector<LocationEntry> m_locations;
    std::vector<uint32_t> m_timestamps;

    // Sector allocation map: true if sector is used
    std::vector<bool> m_sector_free;
    bool m_dirty_image = false; // in-memory changes not yet flushed

    void load_image();        // main -> .bak -> fresh 8KB
    void read_header();
    void sync_header_bytes(); // locations+timestamps -> m_image
    void flush_image();       // write_atomic when dirty
    uint32_t allocate_sectors(uint8_t requested_sectors);
    void free_sectors(uint32_t offset, uint8_t count);

    // Zlib utilities
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& data);
};

} // namespace mc

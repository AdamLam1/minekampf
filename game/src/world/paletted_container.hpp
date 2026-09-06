#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "core/config.hpp"
#include "core/types.hpp"
#include "world/block.hpp"

namespace mc {

// Paletted container (PHASE2 §1.1-1.3): bit-packed indices into a small
// palette of unique block states. Cuts section memory from 8 KB (raw u16) to
// ~2 KB typical. Supports dynamic palette resizing.
//
// Layout: Y-major indexing (y*256 + z*16 + x). Min 4 bits, grows by 1 bit
// each time the palette outgrows the current width (up to 8 = 256 entries,
// which exceeds our block registry size).
class PalettedContainer {
public:
    PalettedContainer() : bits_(4) {
        palette_.push_back(BLOCK_AIR);
        data_.assign(word_count_for(bits_), 0);
    }

    [[nodiscard]] BlockId get(int x, int y, int z) const {
        if (palette_.size() == 1) return palette_[0];
        return palette_[get_packed(section_index_3d(x, y, z))];
    }

    void set(int x, int y, int z, BlockId state) {
        uint16_t idx = palette_index_for(state);
        set_packed(section_index_3d(x, y, z), idx);
    }

    // Direct linear access (used by meshers / gen for speed).
    [[nodiscard]] BlockId get_linear(int index) const {
        if (palette_.size() == 1) return palette_[0];
        return palette_[get_packed(index)];
    }
    void set_linear(int index, BlockId state) {
        uint16_t idx = palette_index_for(state);
        set_packed(index, idx);
    }

    [[nodiscard]] int palette_size() const { return static_cast<int>(palette_.size()); }
    [[nodiscard]] int bits() const { return bits_; }

    // Fast check: is this section entirely one block type? (e.g. all air)
    [[nodiscard]] bool is_single_state() const { return palette_.size() == 1; }
    [[nodiscard]] BlockId single_state() const { return palette_[0]; }
    [[nodiscard]] bool is_all_air() const { return palette_.size() == 1 && palette_[0] == BLOCK_AIR; }

    // Fill the entire section with one block (used for air/stone init).
    void fill(BlockId state) {
        palette_.clear();
        palette_.push_back(state);
        bits_ = 4;
        data_.assign(word_count_for(bits_), 0);
    }

    // -- Codec access (network chunk streaming; save system uses NBT) --
    [[nodiscard]] const std::vector<BlockId>& palette() const { return palette_; }
    [[nodiscard]] const std::vector<uint64_t>& packed_data() const { return data_; }
    // Bulk-restore a serialized container. Input must come from our codec
    // (bits 4..8, palette size <= 1<<bits, data sized for word_count_for).
    void load_packed(int bits, std::vector<BlockId> palette, std::vector<uint64_t> data) {
        bits_ = bits;
        palette_ = std::move(palette);
        data_ = std::move(data);
    }

private:
    static int word_count_for(int bits) {
        return static_cast<int>((SECTION_VOLUME * static_cast<std::size_t>(bits) + 63) / 64);
    }

    [[nodiscard]] uint16_t palette_index_for(BlockId state) {
        for (int i = 0; i < static_cast<int>(palette_.size()); ++i)
            if (palette_[i] == state) return static_cast<uint16_t>(i);
        if (static_cast<int>(palette_.size()) >= (1 << bits_)) {
            resize(bits_ + 1);
        }
        palette_.push_back(state);
        return static_cast<uint16_t>(palette_.size() - 1);
    }

    void resize(int new_bits) {
        if (new_bits > 8) new_bits = 8; // 256 local entries is plenty for our registry
        if (new_bits == bits_) return;
        std::vector<uint64_t> old = data_;
        int old_bits = bits_;
        bits_ = new_bits;
        data_.assign(word_count_for(bits_), 0);
        for (int i = 0; i < SECTION_VOLUME; ++i) {
            set_packed(i, get_packed_from(old, old_bits, i));
        }
    }

    [[nodiscard]] uint16_t get_packed(int index) const { return get_packed_from(data_, bits_, index); }

    static uint16_t get_packed_from(const std::vector<uint64_t>& data, int bits, int index) {
        std::size_t bit_index = static_cast<std::size_t>(index) * bits;
        std::size_t word_index = bit_index >> 6;
        std::size_t bit_offset = bit_index & 63;
        uint64_t mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);
        uint64_t value = (data[word_index] >> bit_offset) & mask;
        std::size_t end = bit_offset + bits;
        if (end > 64) {
            std::size_t overflow = end - 64;
            value |= (data[word_index + 1] << (bits - overflow)) & mask;
        }
        return static_cast<uint16_t>(value);
    }

    void set_packed(int index, uint16_t palette_index) {
        std::size_t bit_index = static_cast<std::size_t>(index) * bits_;
        std::size_t word_index = bit_index >> 6;
        std::size_t bit_offset = bit_index & 63;
        uint64_t mask = (bits_ >= 64) ? ~0ULL : ((1ULL << bits_) - 1);
        uint64_t pval = static_cast<uint64_t>(palette_index) & mask;

        data_[word_index] &= ~(mask << bit_offset);
        data_[word_index] |= pval << bit_offset;

        std::size_t end = bit_offset + bits_;
        if (end > 64) {
            std::size_t overflow = end - 64;
            data_[word_index + 1] &= ~(mask >> (bits_ - overflow));
            data_[word_index + 1] |= pval >> (bits_ - overflow);
        }
    }

    int bits_;
    std::vector<BlockId> palette_;
    std::vector<uint64_t> data_;
};

} // namespace mc

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/config.hpp"
#include "core/types.hpp"
#include "world/block.hpp"
#include "world/dimension.hpp"
#include "world/chunk.hpp"
#include "core/object_pool.hpp"

namespace mc {

// World — owns all loaded chunks and provides block access across chunk
// boundaries (PHASE2 §4). Single-threaded mutation model (Golden Rule #22:
// main thread owns all world mutations).
class World {
public:
    uint64_t seed = 0;
    DimensionId dimension_id = DimensionId::Overworld;

    World() = default;
    explicit World(uint64_t s, DimensionId dim = DimensionId::Overworld) : seed(s), dimension_id(dim) {}

    static ObjectPool<Chunk> chunk_pool;

    [[nodiscard]] Chunk* get_chunk(ChunkPos cp) const {
        auto it = chunks_.find(cp);
        return it == chunks_.end() ? nullptr : it->second.get();
    }

    // Create an empty chunk (all air) and register it. Returns the chunk.
    [[nodiscard]] Chunk& create_chunk(ChunkPos cp) {
        auto& slot = chunks_[cp];
        if (!slot) {
            slot = chunk_pool.acquire();
            slot->reset(cp);
        }
        return *slot;
    }

    // Insert a fully-generated chunk (takes ownership).
    void insert_chunk(std::shared_ptr<Chunk> chunk) {
        ChunkPos cp = chunk->pos;
        chunks_[cp] = std::move(chunk);
        dirty_chunks_.insert(cp);
        // New chunk's blocks may hide faces in neighbor meshes.
        // Mark all 4 cardinal neighbors dirty so they remesh.
        mark_chunk_dirty({cp.x - 1, cp.z});
        mark_chunk_dirty({cp.x + 1, cp.z});
        mark_chunk_dirty({cp.x, cp.z - 1});
        mark_chunk_dirty({cp.x, cp.z + 1});
    }

    void remove_chunk(ChunkPos cp) {
        chunks_.erase(cp);
        dirty_chunks_.erase(cp);
    }

    [[nodiscard]] bool has_chunk(ChunkPos cp) const { return chunks_.contains(cp); }

    // Cross-chunk block access. Returns AIR for unloaded/out-of-bounds.
    [[nodiscard]] BlockId get_block(BlockPos p) const {
        if (p.y < MIN_Y || p.y >= MAX_Y) return BLOCK_AIR;
        Chunk* c = get_chunk(chunk_from_block(p));
        if (!c) return BLOCK_AIR;
        return c->get_block(p);
    }

    // Set a block. Returns false if the owning chunk isn't loaded.
    [[nodiscard]] bool set_block(BlockPos p, BlockId b) {
        if (p.y < MIN_Y || p.y >= MAX_Y) return false;
        ChunkPos cp = chunk_from_block(p);
        Chunk* c = get_chunk(cp);
        if (!c) return false;
        c->set_block(p, b);
        c->light_dirty.store(true, std::memory_order_relaxed); // e.g. torch placed/removed
        dirty_chunks_.insert(cp);
        // Mark neighbors dirty if on the border (their mesh depends on us).
        mark_border_dirty(p);
        return true;
    }

    [[nodiscard]] std::vector<ChunkPos> loaded_positions() const {
        std::vector<ChunkPos> out;
        out.reserve(chunks_.size());
        for (const auto& [cp, _] : chunks_) out.push_back(cp);
        return out;
    }

    [[nodiscard]] std::size_t loaded_count() const { return chunks_.size(); }

    [[nodiscard]] const std::unordered_map<ChunkPos, std::shared_ptr<Chunk>>& chunks() const { return chunks_; }

    std::unordered_set<ChunkPos> dirty_chunks_;

    // Stack-scope block reader with a one-entry chunk cache. Voxel loops
    // (collision sweep, raycast, A* neighbors) hit the same chunk many times
    // in a row; this skips the hash lookup per block. Single-threaded, and
    // the cached pointer must not outlive the reader (chunks can be unloaded
    // between calls).
    class BlockReader {
    public:
        explicit BlockReader(const World& world) : world_(world) {}
        [[nodiscard]] BlockId get_block(BlockPos p) const {
            if (p.y < MIN_Y || p.y >= MAX_Y) return BLOCK_AIR;
            ChunkPos cp = chunk_from_block(p);
            if (cp != last_pos_ || !last_chunk_) {
                last_chunk_ = world_.get_chunk(cp);
                last_pos_ = cp;
            }
            return last_chunk_ ? last_chunk_->get_block(p) : BLOCK_AIR;
        }
    private:
        const World& world_;
        mutable Chunk* last_chunk_ = nullptr;
        mutable ChunkPos last_pos_{INT32_MIN, INT32_MIN};
    };

private:
    void mark_border_dirty(BlockPos p) {
        const int lx = local_x(p.x);
        const int lz = local_z(p.z);
        if (lx == 0) mark_chunk_dirty({(p.x >> 4) - 1, p.z >> 4});
        if (lx == 15) mark_chunk_dirty({(p.x >> 4) + 1, p.z >> 4});
        if (lz == 0) mark_chunk_dirty({p.x >> 4, (p.z >> 4) - 1});
        if (lz == 15) mark_chunk_dirty({p.x >> 4, (p.z >> 4) + 1});
        // Light (e.g. a new torch) bleeds across chunk borders as well.
        if (lx == 0 || lx == 15 || lz == 0 || lz == 15) mark_chunk_light_dirty({p.x >> 4, p.z >> 4});
    }
    void mark_chunk_dirty(ChunkPos cp) {
        if (Chunk* c = get_chunk(cp)) {
            c->mark_dirty();
            dirty_chunks_.insert(cp);
        }
    }
    void mark_chunk_light_dirty(ChunkPos cp) {
        if (Chunk* c = get_chunk(cp)) c->light_dirty.store(true, std::memory_order_relaxed);
    }

    std::unordered_map<ChunkPos, std::shared_ptr<Chunk>> chunks_;
};

} // namespace mc

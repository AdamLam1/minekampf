#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace mc {

// Arena allocator (PHASE1 §2.3): bump-pointer allocation, bulk deallocation.
// O(1) alloc, O(1) reset, zero fragmentation, cache-friendly contiguous layout.
// Intended for per-chunk batch lifetimes.
//
// Golden Rule #3/#4: prefer stack/arena over heap in hot paths.
class Arena {
public:
    Arena() = default;
    explicit Arena(std::size_t capacity) { buffer_.reserve(capacity); reset_to(capacity); }

    // Allocate `size` bytes with `alignment`. Returns nullptr on exhaustion.
    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
        std::size_t aligned = align_up(offset_, alignment);
        if (aligned + size > buffer_.size()) return nullptr;
        offset_ = aligned + size;
        return buffer_.data() + aligned;
    }

    // Bulk-free everything (O(1)). Arena buffer is retained for reuse.
    void reset() { offset_ = 0; }

    void reset_to(std::size_t capacity) {
        buffer_.resize(capacity);
        offset_ = 0;
    }

    [[nodiscard]] std::size_t used_bytes() const { return offset_; }
    [[nodiscard]] std::size_t capacity() const { return buffer_.size(); }

private:
    static std::size_t align_up(std::size_t v, std::size_t a) {
        return (v + a - 1) & ~(a - 1);
    }
    std::vector<std::byte> buffer_;
    std::size_t offset_ = 0;
};

// Typed arena helper: arena-backed construction of T without per-object heap.
template <typename T>
class ArenaAllocator {
public:
    using value_type = T;
    explicit ArenaAllocator(Arena* a) : arena_(a) {}

    template <typename U>
    ArenaAllocator(const ArenaAllocator<U>& o) noexcept : arena_(o.arena()) {}

    [[nodiscard]] T* allocate(std::size_t n) {
        void* p = arena_->allocate(n * sizeof(T), alignof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T*, std::size_t) noexcept {} // arena owns memory; no-op

    [[nodiscard]] Arena* arena() const { return arena_; }
    bool operator==(const ArenaAllocator& o) const { return arena_ == o.arena_; }
    bool operator!=(const ArenaAllocator& o) const { return arena_ != o.arena_; }

private:
    Arena* arena_;
};

} // namespace mc

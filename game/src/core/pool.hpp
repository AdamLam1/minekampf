#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stack>
#include <stdexcept>
#include <vector>

namespace mc {

// Object pool (PHASE1 §2.2): pre-allocate N objects, reuse via free list.
// Avoids per-tick heap allocation for transient BlockPos/Vec3/AABB/etc.
//
// T must be default-constructible. `acquire()` returns an index handle and a
// pointer; `release(index)` returns it to the pool.
template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t capacity) : storage_(capacity) {
        free_.reserve(capacity);
        for (std::size_t i = capacity; i-- > 0;) free_.push_back(i); // LIFO
    }

    [[nodiscard]] T* acquire() {
        if (free_.empty()) throw std::runtime_error("ObjectPool exhausted — increase capacity");
        std::size_t idx = free_.back();
        free_.pop_back();
        return &storage_[idx];
    }

    void release(T* obj) {
        if (obj < storage_.data() || obj >= storage_.data() + storage_.size()) return;
        auto idx = static_cast<std::size_t>(obj - storage_.data());
        free_.push_back(idx);
    }

    [[nodiscard]] std::size_t available() const { return free_.size(); }
    [[nodiscard]] std::size_t capacity() const { return storage_.size(); }

private:
    std::vector<T> storage_;
    std::vector<std::size_t> free_;
};

} // namespace mc

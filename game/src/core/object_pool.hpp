#pragma once

#include <vector>
#include <mutex>
#include <memory>

namespace mc {

/**
 * A thread-safe generic object pool for reusing objects like Chunks.
 * Prevents GC/allocation spikes by recycling memory.
 */
template <typename T>
class ObjectPool {
public:
    ObjectPool() = default;

    ~ObjectPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (T* ptr : pool_) {
            delete ptr;
        }
        pool_.clear();
    }

    // Non-copyable
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    /**
     * Acquires an object from the pool, or allocates a new one if the pool is empty.
     * Returns a std::shared_ptr with a custom deleter that returns the object back to the pool.
     */
    std::shared_ptr<T> acquire() {
        T* raw_ptr = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!pool_.empty()) {
                raw_ptr = pool_.back();
                pool_.pop_back();
            }
        }

        if (!raw_ptr) {
            raw_ptr = new T();
        }

        return std::shared_ptr<T>(raw_ptr, [this](T* ptr) {
            this->release(ptr);
        });
    }

    /**
     * Pre-allocates a specific number of objects.
     */
    void reserve(std::size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < count; ++i) {
            pool_.push_back(new T());
        }
    }

    /**
     * Returns the number of objects currently available in the pool.
     */
    std::size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }

private:
    void release(T* ptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.push_back(ptr);
    }

    mutable std::mutex mutex_;
    std::vector<T*> pool_;
};

} // namespace mc

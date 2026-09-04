#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace mc {

// Thread-safe bounded-ish queue: message-passing channel between
// main and worker threads. One end pushes, the other drains.
//
// Deviation note: a lock-free MPMC queue (e.g. moodycamel) was considered.
// We use a mutex+condition_variable queue: simpler, correct, and the actual
// throughput for the game's coarse-grained tasks is not the bottleneck.
template <typename T>
class Channel {
public:
    void push(T item) {
        {
            std::lock_guard lock(mtx_);
            queue_.push_back(std::move(item));
        }
        cv_.notify_one();
    }

    // Blocking pop.
    [[nodiscard]] T pop() {
        std::unique_lock lock(mtx_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

    // Non-blocking pop; returns false if empty.
    [[nodiscard]] bool try_pop(T& out) {
        std::lock_guard lock(mtx_);
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard lock(mtx_);
        return queue_.empty();
    }

    std::size_t size() const {
        std::lock_guard lock(mtx_);
        return queue_.size();
    }

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<T> queue_;
};

// Worker thread pool (PHASE1 §3.1). Main thread submits tasks; workers run
// them. Results are communicated back via Channels (message passing, not
// shared mutable state — Golden Rule #21/#22).
class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count = 0) {
        if (thread_count == 0) {
            thread_count = std::max<std::size_t>(2u, std::thread::hardware_concurrency());
        }
        workers_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool() { shutdown(); }

    void shutdown() {
        if (stop_.exchange(true)) return;
        cv_.notify_all();
        for (auto& w : workers_)
            if (w.joinable()) w.join();
    }

    // Submit a unit of work.
    void submit(std::function<void()> task) {
        {
            std::lock_guard lock(mtx_);
            tasks_.push_back(std::move(task));
            active_tasks_++;
        }
        cv_.notify_one();
    }

    // Wait for all submitted tasks to complete.
    void wait() {
        std::unique_lock lock(mtx_);
        wait_cv_.wait(lock, [this] { return active_tasks_ == 0; });
    }

    [[nodiscard]] std::size_t size() const { return workers_.size(); }

private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(mtx_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            try {
                task();
            } catch (const std::exception& e) {
                // A worker must never die silently: log and keep the pool alive.
                spdlog::error("ThreadPool worker exception: {}", e.what());
            } catch (...) {
                spdlog::error("ThreadPool worker unknown exception");
            }
            {
                std::lock_guard lock(mtx_);
                active_tasks_--;
                if (active_tasks_ == 0) {
                    wait_cv_.notify_all();
                }
            }
        }
    }

    std::vector<std::jthread> workers_;
    std::deque<std::function<void()>> tasks_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::condition_variable wait_cv_;
    std::atomic<int> active_tasks_{0};
    std::atomic<bool> stop_{false};
};

} // namespace mc

#pragma once

#include <chrono>

#include "core/config.hpp"

namespace mc {

// Fixed-timestep accumulator (PHASE1 §1.1, "Fix Your Timestep").
// Calls `tick()` exactly at SERVER_TPS; lets the caller interpolate render.
class FixedTimestepLoop {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;

    FixedTimestepLoop() : last_(Clock::now()) {}

    // Run pending ticks for elapsed wall-clock time. Returns ticks executed.
    template <typename TickFn>
    int update(TickFn&& tick) {
        const auto now = Clock::now();
        accumulator_ += now - last_;
        last_ = now;

        // Prevent spiral-of-death after a stall (e.g. debugger pause).
        if (accumulator_ > max_lag_) accumulator_ = max_lag_;

        int executed = 0;
        while (accumulator_ >= tick_interval_) {
            tick();
            accumulator_ -= tick_interval_;
            ++executed;
        }
        return executed;
    }

    // Interpolation alpha in [0,1): how far we are into the next tick.
    [[nodiscard]] float alpha() const {
        return static_cast<float>(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                        accumulator_).count()) /
                                  static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                         tick_interval_).count()));
    }

    void reset() {
        last_ = Clock::now();
        accumulator_ = Duration::zero();
    }

private:
    Duration tick_interval_ = std::chrono::duration_cast<Duration>(std::chrono::duration<double>(TICK_INTERVAL_SEC));
    Duration max_lag_ = std::chrono::duration_cast<Duration>(std::chrono::duration<double>(0.25)); // 250ms cap
    Duration accumulator_{};
    Clock::time_point last_;
};

// Frame delta timer for the variable-timestep client loop (PHASE1 §1.2).
class FrameTimer {
public:
    using Clock = std::chrono::steady_clock;

    FrameTimer() : last_(Clock::now()) {}

    [[nodiscard]] float delta_seconds() {
        const auto now = Clock::now();
        const float dt = std::chrono::duration<float>(now - last_).count();
        last_ = now;
        // Clamp huge deltas (alt-tab, breakpoint) to keep physics sane.
        return dt > 0.25f ? 0.25f : dt;
    }

private:
    Clock::time_point last_;
};

} // namespace mc

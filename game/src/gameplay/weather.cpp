#include "gameplay/weather.hpp"
#include <cstdlib>

namespace mc {

void WeatherSystem::tick() {
    if (ticks_until_next_state_ > 0) {
        ticks_until_next_state_--;
    } else {
        // Switch state
        int r = std::rand() % 100;
        if (state_ == WeatherState::Clear) {
            if (r < 80) state_ = WeatherState::Rain;
            else state_ = WeatherState::Thunder;
            ticks_until_next_state_ = 6000 + (std::rand() % 6000); // 5 to 10 mins
        } else {
            state_ = WeatherState::Clear;
            ticks_until_next_state_ = 12000 + (std::rand() % 24000); // 10 to 30 mins
        }
    }

    // Smooth intensity interpolation (~1 s to full strength so weather
    // changes feel responsive and are testable without long waits).
    float target_intensity = (state_ == WeatherState::Clear) ? 0.0f : 1.0f;
    if (intensity_ < target_intensity) {
        intensity_ += 0.02f;
        if (intensity_ > target_intensity) intensity_ = target_intensity;
    } else if (intensity_ > target_intensity) {
        intensity_ -= 0.02f;
        if (intensity_ < target_intensity) intensity_ = target_intensity;
    }
}

} // namespace mc

#pragma once

namespace mc {

enum class WeatherState {
    Clear,
    Rain,
    Thunder
};

class WeatherSystem {
public:
    WeatherSystem() = default;

    void tick();

    WeatherState current_state() const { return state_; }
    float intensity() const { return intensity_; }
    float sky_darkening() const { return intensity_ * 0.5f; }
    // Debug/commands: jump straight to a state (intensity keeps smoothing).
    void force(WeatherState s) {
        state_ = s;
        ticks_until_next_state_ = (s == WeatherState::Clear) ? 12000 : 6000;
    }

private:
    WeatherState state_ = WeatherState::Clear;
    int ticks_until_next_state_ = 6000; // 5 mins
    float intensity_ = 0.0f; // 0.0 to 1.0 (smooth transition)
};

} // namespace mc

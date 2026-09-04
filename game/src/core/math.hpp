#pragma once

#include <cmath>
#include <glm/glm.hpp>

#include "core/types.hpp"

namespace mc {

[[nodiscard]] constexpr float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

[[nodiscard]] constexpr double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

[[nodiscard]] constexpr float lerpf(float a, float b, float t) { return a + t * (b - a); }

[[nodiscard]] constexpr double lerpd(double a, double b, double t) { return a + t * (b - a); }

// Quintic fade curve (Perlin "Improving Noise" 2002): 6t^5 - 15t^4 + 10t^3
[[nodiscard]] inline double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

[[nodiscard]] inline float length_xz(Vec3 v) { return std::sqrt(v.x * v.x + v.z * v.z); }

// Wrap an angle (radians) into [-pi, pi] — for shortest-path yaw interp.
[[nodiscard]] inline float wrap_angle(float a) {
    constexpr float TWO_PI = 6.28318530718f;
    constexpr float PI = 3.14159265359f;
    a = std::fmod(a + PI, TWO_PI);
    if (a < 0.0f) a += TWO_PI;
    return a - PI;
}

// Shortest-path lerp between two angles (radians).
[[nodiscard]] inline float lerp_angle(float a, float b, float t) {
    float diff = wrap_angle(b - a);
    return a + diff * t;
}

// Build a right-handed look direction from yaw/pitch (radians).
// yaw=0 -> -Z, pitch=0 -> horizontal; matches Minecraft conventions.
[[nodiscard]] inline Vec3 forward_from_yaw_pitch(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    return Vec3(-std::sin(yaw) * cp, -std::sin(pitch), std::cos(yaw) * cp);
}

} // namespace mc

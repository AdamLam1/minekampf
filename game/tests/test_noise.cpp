#include <gtest/gtest.h>

#include <algorithm>

#include "generation/noise.hpp"

using mc::PerlinNoise;

TEST(Noise, RangeWithinUnit) {
    PerlinNoise n(12345ULL);
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 1000; ++i) {
        double v = n.noise3(i * 0.13, i * 0.27, i * 0.41);
        lo = std::min(lo, static_cast<float>(v));
        hi = std::max(hi, static_cast<float>(v));
    }
    EXPECT_GE(lo, -1.01f);
    EXPECT_LE(hi, 1.01f);
}

TEST(Noise, Deterministic) {
    PerlinNoise a(42ULL), b(42ULL);
    for (int i = 0; i < 100; ++i) {
        EXPECT_DOUBLE_EQ(a.noise3(i * 0.1, i * 0.2, i * 0.3), b.noise3(i * 0.1, i * 0.2, i * 0.3));
    }
}

TEST(Noise, DifferentSeedsDiffer) {
    PerlinNoise a(1ULL), b(2ULL);
    bool any_diff = false;
    for (int i = 0; i < 100; ++i) {
        if (a.noise3(i * 0.1, i * 0.2, i * 0.3) != b.noise3(i * 0.1, i * 0.2, i * 0.3)) {
            any_diff = true;
            break;
        }
    }
    EXPECT_TRUE(any_diff);
}

TEST(Noise, FbmInUnitRange) {
    PerlinNoise n(7ULL);
    for (int i = 0; i < 200; ++i) {
        double v = n.fbm2(i * 0.05, i * 0.07, 6);
        EXPECT_GE(v, -1.01);
        EXPECT_LE(v, 1.01);
    }
}

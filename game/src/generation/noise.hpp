#pragma once

#include <cmath>
#include <cstdint>

#include "core/random.hpp"

namespace mc {

// Improved Perlin noise (Ken Perlin 2002) with quintic interpolation and a
// seeded permutation table (PHASE6 §1.1). Deterministic given the seed.
class PerlinNoise {
public:
    PerlinNoise() = default;
    explicit PerlinNoise(uint64_t seed) { init(seed); }

    void init(uint64_t seed) {
        Rng rng(seed);
        uint8_t p[256];
        for (int i = 0; i < 256; ++i) p[i] = static_cast<uint8_t>(i);
        // Fisher-Yates shuffle with seeded PRNG.
        for (int i = 255; i > 0; --i) {
            int j = rng.next_int(i + 1);
            uint8_t tmp = p[i];
            p[i] = p[j];
            p[j] = tmp;
        }
        for (int i = 0; i < 256; ++i) {
            perm_[i] = p[i];
            perm_[i + 256] = p[i];
        }
    }

    // 3D Perlin noise, output range ~[-1, 1].
    [[nodiscard]] double noise3(double x, double y, double z) const {
        int xi = static_cast<int>(std::floor(x)) & 255;
        int yi = static_cast<int>(std::floor(y)) & 255;
        int zi = static_cast<int>(std::floor(z)) & 255;

        double xf = x - std::floor(x);
        double yf = y - std::floor(y);
        double zf = z - std::floor(z);

        double u = fade(xf);
        double v = fade(yf);
        double w = fade(zf);

        int aaa = perm_[perm_[perm_[xi] + yi] + zi];
        int aba = perm_[perm_[perm_[xi] + yi + 1] + zi];
        int aab = perm_[perm_[perm_[xi] + yi] + zi + 1];
        int abb = perm_[perm_[perm_[xi] + yi + 1] + zi + 1];
        int baa = perm_[perm_[perm_[xi + 1] + yi] + zi];
        int bba = perm_[perm_[perm_[xi + 1] + yi + 1] + zi];
        int bab = perm_[perm_[perm_[xi + 1] + yi] + zi + 1];
        int bbb = perm_[perm_[perm_[xi + 1] + yi + 1] + zi + 1];

        double x1 = lerp(u, grad(aaa, xf, yf, zf), grad(baa, xf - 1, yf, zf));
        double x2 = lerp(u, grad(aba, xf, yf - 1, zf), grad(bba, xf - 1, yf - 1, zf));
        double y1 = lerp(v, x1, x2);

        x1 = lerp(u, grad(aab, xf, yf, zf - 1), grad(bab, xf - 1, yf, zf - 1));
        x2 = lerp(u, grad(abb, xf, yf - 1, zf - 1), grad(bbb, xf - 1, yf - 1, zf - 1));
        double y2 = lerp(v, x1, x2);

        return lerp(w, y1, y2); // ~[-1,1]
    }

    // 2D Perlin (Y dimension unused) for heightmap-style noise.
    [[nodiscard]] double noise2(double x, double z) const { return noise3(x, 0.0, z); }

    // Fractal Brownian Motion (PHASE6 §1.2): sum of octaves.
    [[nodiscard]] double fbm3(double x, double y, double z, int octaves, double lacunarity = 2.0,
                              double persistence = 0.5) const {
        double total = 0.0, amplitude = 1.0, frequency = 1.0, max_value = 0.0;
        for (int i = 0; i < octaves; ++i) {
            total += noise3(x * frequency, y * frequency, z * frequency) * amplitude;
            max_value += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }
        return total / max_value; // ~[-1,1]
    }

    [[nodiscard]] double fbm2(double x, double z, int octaves, double lacunarity = 2.0,
                              double persistence = 0.5) const {
        return fbm3(x, 0.0, z, octaves, lacunarity, persistence);
    }

private:
    static double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
    static double lerp(double t, double a, double b) { return a + t * (b - a); }
    static double grad(int hash, double x, double y, double z) {
        int h = hash & 15;
        double u = h < 8 ? x : y;
        double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

    uint8_t perm_[512]{};
};

} // namespace mc

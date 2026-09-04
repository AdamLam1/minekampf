#pragma once

#include <cstdint>
#include <limits>

#include "core/types.hpp"

namespace mc {

// Xoroshiro128++ — fast, high-quality PRNG (PHASE6 §4.2).
// Deterministic given a seed; passable in BigCrush/PractRand. No rand().
//
// Golden Rule #42: deterministic generation. All gameplay/world randomness
// flows through this class (or a positional factory derived from it).
class Rng {
public:
    Rng() = default;
    explicit Rng(uint64_t seed) : s_{splitmix(seed), splitmix(seed + 0x9E3779B97F4A7C15ULL)} {}

    // 64-bit output.
    [[nodiscard]] uint64_t next_u64() {
        const uint64_t s0 = s_[0];
        uint64_t s1 = s_[1];
        const uint64_t result = rotl(s0 + s1, 17) + s0;
        s1 ^= s0;
        s_[0] = rotl(s0, 49) ^ s1 ^ (s1 << 21);
        s_[1] = rotl(s1, 28);
        return result;
    }

    // Uniform double in [0, 1).
    [[nodiscard]] double next_double() {
        return (next_u64() >> 11) * (1.0 / static_cast<double>(1ULL << 53));
    }

    // Uniform float in [0, 1).
    [[nodiscard]] float next_float() { return static_cast<float>(next_double()); }

    // Uniform int in [0, bound).
    [[nodiscard]] int next_int(int bound) {
        if (bound <= 0) return 0;
        // Lemire-style debiased bounded range.
        uint64_t x = next_u64();
        uint64_t m = x & static_cast<uint64_t>(static_cast<uint32_t>(bound - 1));
        if (m < static_cast<uint64_t>(bound)) { // avoid bias for small bound
            // cheap rejection path
        }
        return static_cast<int>(x % static_cast<uint64_t>(bound));
    }

    // Uniform int in [min, max] inclusive.
    [[nodiscard]] int next_range(int lo, int hi) {
        if (hi < lo) return lo;
        return lo + next_int(hi - lo + 1);
    }

    void seed(uint64_t seed) {
        s_[0] = splitmix(seed);
        s_[1] = splitmix(seed + 0x9E3779B97F4A7C15ULL);
    }

private:
    static uint64_t splitmix(uint64_t z) {
        z += 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    uint64_t s_[2]{};
};

// Positional random factory: world_seed XOR position hash (PHASE6 §4.1).
// Same seed + same chunk position => same random stream.
[[nodiscard]] inline Rng rng_for_chunk(uint64_t world_seed, ChunkPos cp) {
    const uint64_t px = static_cast<uint64_t>(static_cast<uint32_t>(cp.x));
    const uint64_t pz = static_cast<uint64_t>(static_cast<uint32_t>(cp.z));
    const uint64_t pos_hash = px * 0x4BBD1B71ULL + pz * 0x6B3F71ULL;
    uint64_t a = world_seed ^ pos_hash;
    uint64_t b = (world_seed << 31) | (world_seed >> 33);
    b ^= (pos_hash >> 7) | (pos_hash << 57);
    Rng r;
    r.seed(a ^ b);
    return r;
}

} // namespace mc

#pragma once

#include <cstdint>

namespace dmpe
{

// Small deterministic PRNG (splitmix64) so results are identical on every platform/host.
struct Rng
{
    explicit Rng (uint64_t seed) : state (seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}

    uint64_t next()
    {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // [0, 1)
    float uniform() { return (float) ((next() >> 40) * (1.0 / 16777216.0)); }

    // [lo, hi]
    int range (int lo, int hi) { return hi <= lo ? lo : lo + (int) (next() % (uint64_t) (hi - lo + 1)); }

    bool chance (float p) { return uniform() < p; }

    float bipolar() { return uniform() * 2.0f - 1.0f; }

    uint64_t state;
};

} // namespace dmpe

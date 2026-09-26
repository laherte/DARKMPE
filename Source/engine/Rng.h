#pragma once

#include <cstdint>
#include <initializer_list>

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

// A generator for one decision point, keyed by what the decision is about (seed, bar or section, position...)
// instead of by how many numbers were drawn before it: changing the number of bars or notes elsewhere never
// shifts it.
inline Rng keyedRng (std::initializer_list<uint64_t> keys)
{
    uint64_t h = 0x243F6A8885A308D3ull;
    for (uint64_t k : keys)
    {
        h ^= k + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h = (h ^ (h >> 31)) * 0xBF58476D1CE4E5B9ull;
    }
    return Rng (h);
}

inline uint64_t hashLabel (const char* label)
{
    uint64_t h = 1469598103934665603ull;
    for (; *label != 0; ++label)
        h = (h ^ (uint64_t) (unsigned char) *label) * 1099511628211ull;
    return h;
}

} // namespace dmpe

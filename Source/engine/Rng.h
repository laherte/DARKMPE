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

// FNV-1a of a section label ("A", "B'", ...): the same label gives the same random choices.
inline uint64_t labelHash (const char* label)
{
    uint64_t h = 1469598103934665603ull;
    for (; *label != 0; ++label)
        h = (h ^ (uint64_t) (unsigned char) *label) * 1099511628211ull;
    return h;
}

// Combines seed parts into one well-spread, never-zero seed (0 means "no seed" for Note::exprSeed).
inline uint64_t mixSeed (uint64_t a, uint64_t b)
{
    uint64_t z = a * 0x9E3779B97F4A7C15ull + b + 0x7F4A7C159E3779B9ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return z == 0 ? 1 : z;
}

} // namespace dmpe

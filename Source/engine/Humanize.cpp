#include "Humanize.h"
#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace dmpe
{

void humanize (Phrase& phrase, float amount, int seed)
{
    if (amount <= 0.0f)
        return;

    // Start times that a glide ties into: the note ending there must not move either.
    const auto key = [] (double t) { return (long long) std::llround (t * 1.0e6); };
    std::set<long long> glideStarts;
    for (const auto& n : phrase.notes)
        if (n.glideFrom >= 0)
            glideStarts.insert (key (n.start));

    Rng rng ((uint64_t) seed * 48271ull + 7ull);
    for (auto& n : phrase.notes)
    {
        const float dv = rng.bipolar() * 0.15f * amount;
        const double dt = rng.bipolar() * (1.0 / 64.0) * (double) amount;
        if (n.lockedExpr)
            continue;

        n.velocity = std::clamp (n.velocity + dv, 0.05f, 1.0f);

        const bool tied = n.glideFrom >= 0 || glideStarts.count (key (n.end())) != 0;
        if (tied)
            continue;
        const double end = n.end();
        n.start = std::clamp (n.start + dt, 0.0, std::max (0.0, end - 1.0 / 64.0));
        n.length = end - n.start;
    }
    phrase.sortByStart();
}

} // namespace dmpe

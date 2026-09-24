#include "Phrase.h"

#include <algorithm>

namespace dmpe
{

float evalCurve (const Curve& c, double t, float def)
{
    if (c.empty())
        return def;

    if (t <= c.front().t)
        return c.front().v;

    if (t >= c.back().t)
        return c.back().v;

    auto it = std::upper_bound (c.begin(), c.end(), t, [] (double x, const CurvePoint& p) { return x < p.t; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double span = b.t - a.t;

    if (span <= 0.0)
        return b.v;

    return a.v + (float) ((t - a.t) / span) * (b.v - a.v);
}

void Phrase::sortByStart()
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
    {
        if (a.start < b.start || b.start < a.start)
            return a.start < b.start;
        return a.pitch < b.pitch;
    });
}

} // namespace dmpe

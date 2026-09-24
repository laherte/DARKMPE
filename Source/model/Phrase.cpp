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

float CurveCursor::at (double t)
{
    if (curve.empty())
        return fallback;
    if (t <= curve.front().t)
        return curve.front().v;

    while (index + 1 < curve.size() && curve[index + 1].t <= t)
        ++index;
    if (index + 1 >= curve.size())
        return curve.back().v;

    const auto& a = curve[index];
    const auto& b = curve[index + 1];
    const double span = b.t - a.t;
    return span <= 0.0 ? b.v : a.v + (float) ((t - a.t) / span) * (b.v - a.v);
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

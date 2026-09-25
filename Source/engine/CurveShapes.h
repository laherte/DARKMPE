#pragma once

#include "model/Phrase.h"
#include "engine/Scales.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dmpe
{

// How a pitch transition moves from one value to the next.
enum class GlideShape { linear, ease, swoopIn, swoopOut, stepped, count };
inline const char* const glideShapeNames[] = { "Linear", "Ease", "Swoop In", "Swoop Out", "Stepped" };

inline float shapeCurve (GlideShape s, float x)
{
    x = std::clamp (x, 0.0f, 1.0f);
    switch (s)
    {
        case GlideShape::ease:
        case GlideShape::stepped:  return x * x * (3.0f - 2.0f * x);
        case GlideShape::swoopIn:  return x * x * x;
        case GlideShape::swoopOut: return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
        case GlideShape::linear:
        case GlideShape::count:    break;
    }
    return x;
}

inline float smoothstep (float a, float b, float x)
{
    const float t = std::clamp ((x - a) / std::max (1.0e-6f, b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Where Stepped glides may stop: the notes of this key / scale.
struct StepContext
{
    int key;
    scales::Scale scale;
};

// Appends a shaped transition (from -> to between t0 and t1) to a curve, keeping it time-ordered.
// Offsets are semitones above `basePitch`; a Stepped transition walks through the scale notes in between,
// sliding quickly into each one and holding it (a brass / harp glissando).
inline void addTransition (Curve& c, double t0, double t1, float from, float to, GlideShape shape,
                           int basePitch = 0, const StepContext* steps = nullptr)
{
    t0 = std::max (t0, c.empty() ? 0.0 : c.back().t);
    t1 = std::max (t1, t0 + 1.0e-3);
    c.push_back ({ t0, from });

    if (shape == GlideShape::stepped && steps != nullptr)
    {
        std::vector<float> path;
        const int a = basePitch + (int) std::lround (from), b = basePitch + (int) std::lround (to);
        const int dir = b > a ? 1 : -1;
        for (int p = a + dir; p != b && a != b; p += dir)
            if (scales::inScale (p, steps->key, steps->scale))
                path.push_back ((float) (p - basePitch));
        path.push_back (to);

        const double seg = (t1 - t0) / (double) path.size();
        float cur = from;
        for (size_t i = 0; i < path.size(); ++i)
        {
            const double s0 = t0 + seg * (double) i;
            for (int j = 1; j <= 4; ++j)
            {
                const float x = (float) j / 4.0f;
                c.push_back ({ s0 + seg * 0.3 * x, cur + (path[i] - cur) * shapeCurve (GlideShape::ease, x) });
            }
            cur = path[i];
        }
        c.push_back ({ t1, to });
        return;
    }

    constexpr int stepsPerTransition = 16;
    for (int i = 1; i <= stepsPerTransition; ++i)
    {
        const float x = (float) i / stepsPerTransition;
        c.push_back ({ t0 + (t1 - t0) * x, from + (to - from) * shapeCurve (shape, x) });
    }
}

} // namespace dmpe

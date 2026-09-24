#include "ExpressionShaper.h"
#include "Rng.h"

#include <algorithm>
#include <cmath>

namespace dmpe
{

void shapeExpression (Phrase& phrase, const ExprParams& p)
{
    constexpr double twoPi = 6.283185307179586;
    Rng rng ((uint64_t) p.seed * 2654435761ull + 3ull);

    for (auto& n : phrase.notes)
    {
        if (n.lockedExpr)
        {
            // Engine-authored motion: keep it, add the analog detune spread only.
            const float pos = n.voiceCount > 1 ? (float) n.voiceIndex / (float) (n.voiceCount - 1) : 0.5f;
            const float detune = (rng.bipolar() * 0.15f + (n.voiceCount > 1 ? pos * 2.0f - 1.0f : 0.0f)) * p.detuneCents / 100.0f;
            if (n.bend.empty())
                n.bend.push_back ({ 0.0, 0.0f });
            for (auto& pt : n.bend)
                pt.v += detune;
            n.releaseVelocity = 0.3f;
            continue;
        }

        const bool keep = p.keepInput && n.hasSourceExpr;
        const Curve srcBend = keep ? n.bend : Curve {};
        const Curve srcSlide = keep ? n.slide : Curve {};
        const Curve srcPress = keep ? n.pressure : Curve {};

        n.bend.clear();
        n.slide.clear();
        n.pressure.clear();

        const double len = std::max (0.01, n.length);
        const float voicePos = n.voiceCount > 1 ? (float) n.voiceIndex / (float) (n.voiceCount - 1) : 0.5f;

        // Pitch: static detune + glide-in + delayed vibrato.
        float detune = rng.bipolar() * p.detuneCents * 0.15f / 100.0f;
        if (n.voiceCount > 1)
            detune += (voicePos * 2.0f - 1.0f) * p.detuneCents / 100.0f;

        const float glideAmount = n.glideFrom >= 0 ? (float) std::clamp (n.glideFrom - n.pitch, -48, 48) : 0.0f;
        const double glideLen = std::max (0.005, std::min ((double) p.glideTime, len * 0.85));
        const float exponent = 1.0f + 3.0f * p.glideCurve;
        const bool vibrato = srcBend.empty() && p.vibratoDepth > 0.0f && len > p.vibratoDelay + 0.25;
        const double vibPhase = rng.uniform();

        // Timbre and pressure targets.
        const float reg = std::clamp ((float) (n.pitch - 36) / 60.0f, 0.0f, 1.0f);
        const float slideBase = std::clamp (0.25f + p.slideSpread * (reg - 0.5f) * 0.6f
                                              + p.slideSpread * 0.3f * (voicePos - 0.5f)
                                              + (n.accent ? 0.1f : 0.0f), 0.0f, 1.0f);
        const float pressStart = 0.1f + n.velocity * 0.5f * p.pressureAmount;
        const float pressPeak = 0.35f + 0.6f * p.pressureAmount;
        const double breathPhase = voicePos * 0.5 + rng.uniform() * 0.2;

        const int steps = std::max (1, (int) std::ceil (len / curveResolution));
        for (int i = 0; i <= steps; ++i)
        {
            const double t = std::min (len, i * curveResolution);

            float bend = detune + evalCurve (srcBend, t, 0.0f);
            if (glideAmount != 0.0f && t < glideLen)
                bend += glideAmount * std::pow (1.0f - (float) (t / glideLen), exponent);
            if (vibrato && t > p.vibratoDelay)
            {
                const float ramp = std::clamp ((float) ((t - p.vibratoDelay) / 0.5), 0.0f, 1.0f);
                bend += p.vibratoDepth * ramp * (float) std::sin (twoPi * (p.vibratoRate * (t - p.vibratoDelay) + vibPhase));
            }
            n.bend.push_back ({ t, bend });

            const double effLen = std::max (len, 0.25);
            const float open = p.slideAmount * 0.7f * (1.0f - (float) std::exp (-3.0 * t / effLen));
            n.slide.push_back ({ t, srcSlide.empty() ? std::clamp (slideBase + open, 0.0f, 1.0f)
                                                     : evalCurve (srcSlide, t, 0.5f) });

            const float x = (float) (t / len);
            float press = x < 0.6f ? pressStart + (pressPeak - pressStart) * (x / 0.6f)
                                   : pressPeak * (1.0f - 0.3f * (x - 0.6f) / 0.4f);
            press += p.breath * 0.15f * (float) std::sin (twoPi * (0.25 * (n.start + t) + breathPhase));
            n.pressure.push_back ({ t, srcPress.empty() ? std::clamp (press, 0.0f, 1.0f)
                                                        : evalCurve (srcPress, t, 0.0f) });
        }

        n.releaseVelocity = std::clamp (0.3f + 0.4f * n.velocity, 0.0f, 1.0f);
    }
}

} // namespace dmpe

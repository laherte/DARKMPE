#include "ExpressionShaper.h"
#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dmpe
{

void shapeExpression (Phrase& phrase, const ExprParams& p)
{
    constexpr double twoPi = 6.283185307179586;
    Rng shared ((uint64_t) p.seed * 2654435761ull + 3ull);

    for (auto& n : phrase.notes)
    {
        // Notes with their own seed get the same detune / vibrato phase wherever their section comes back.
        Rng own (mixSeed (n.exprSeed, (uint64_t) p.seed * 2654435761ull + 3ull));
        Rng& rng = n.exprSeed != 0 ? own : shared;

        if (n.lockedExpr)
        {
            // Engine-authored motion: keep it, add the analog detune spread only.
            const float pos = n.voiceCount > 1 ? (float) n.voiceIndex / (float) (n.voiceCount - 1) : 0.5f;
            const float detune = (rng.bipolar() * 0.15f + (n.voiceCount > 1 ? pos * 2.0f - 1.0f : 0.0f)) * p.detuneCents / 100.0f;
            if (n.bend.empty())
                n.bend.push_back ({ 0.0, 0.0f });
            if (! n.gesture.empty())
            {
                // A gesture on top of the authored motion (e.g. a stab's fall): both curves, on the union of their points.
                std::vector<double> times;
                for (const auto* c : { &n.bend, &n.gesture })
                    for (const auto& pt : *c)
                        times.push_back (std::max (0.0, pt.t));
                std::sort (times.begin(), times.end());
                times.erase (std::unique (times.begin(), times.end(), [] (double x, double y) { return y - x < 1.0e-6; }), times.end());
                Curve merged;
                for (const double t : times)
                    merged.push_back ({ t, evalCurve (n.bend, t, 0.0f) + evalCurve (n.gesture, t, 0.0f) });
                n.bend = std::move (merged);
            }
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

        // Detune (cents): stacked voices spread apart; every note also gets its own offset and a slow analog
        // drift, so a single line is audibly (and visibly) detuned too.
        const float spread = n.voiceCount > 1 ? (voicePos * 2.0f - 1.0f) * p.detuneCents : 0.0f;
        const float offset = rng.bipolar() * p.detuneCents * (n.voiceCount > 1 ? 0.15f : 0.5f);
        const float driftDepth = p.detuneCents * 0.3f;
        const double driftRate = 0.3 + 0.3 * rng.uniform(), driftPhase = rng.uniform();

        // Glide-in from the previous pitch (unless a gesture authored its own).
        const float glideAmount = n.glideFrom >= 0 && ! n.glideAuthored ? (float) std::clamp (n.glideFrom - n.pitch, -48, 48) : 0.0f;
        const double glideLen = std::max (0.005, std::min ((double) p.glideTime, len * 0.85));
        const float exponent = 1.0f + 3.0f * p.glideCurve;

        // Vibrato: the delay and fade-in scale with the note, so any note long enough for 3/4 of a cycle after
        // its delay sings (a 1-beat note at the default rate does; 16ths don't).
        const float vibDepth = p.vibratoDepth * std::max (0.0f, n.vibrato);
        const double vibRate = std::max (0.05f, p.vibratoRate);
        const double vibDelay = std::min ((double) p.vibratoDelay, 0.4 * len);
        const double vibFade = std::clamp (0.3 * len, 0.05, 0.5);
        const bool vibrato = srcBend.empty() && vibDepth > 0.0f && len - vibDelay >= 0.75 / vibRate;
        const double vibPhase = rng.uniform();

        // Timbre and pressure targets.
        const float reg = std::clamp ((float) (n.pitch - 36) / 60.0f, 0.0f, 1.0f);
        const float slideBase = std::clamp (0.25f + p.slideSpread * (reg - 0.5f) * 0.6f
                                              + p.slideSpread * 0.3f * (voicePos - 0.5f)
                                              + (n.accent ? 0.1f : 0.0f), 0.0f, 1.0f);
        const float pressStart = 0.1f + n.velocity * 0.5f * p.pressureAmount;
        const float pressPeak = 0.35f + 0.6f * p.pressureAmount;
        const double breathPhase = voicePos * 0.5 + rng.uniform() * 0.2;

        // Sample times: dense where the pitch moves fast (glide-in, vibrato), 1/32 beat elsewhere, plus every
        // point of the authored gesture curves so their shape is kept exactly.
        std::vector<double> times;
        for (double t = 0.0;;)
        {
            times.push_back (t);
            if (t >= len)
                break;
            double step = curveResolution;
            if (glideAmount != 0.0f && t < glideLen)
                step = std::min (step, glideLen / 48.0);
            if (vibrato && t + curveResolution > vibDelay)
                step = std::min (step, 1.0 / (16.0 * vibRate));
            t = std::min (len, t + step);
        }
        for (const auto* c : { &n.gesture, &n.gestureTimbre, &n.gesturePress })
            for (const auto& pt : *c)
                if (pt.t > 0.0 && pt.t < len)
                    times.push_back (pt.t);
        std::sort (times.begin(), times.end());
        times.erase (std::unique (times.begin(), times.end(), [] (double x, double y) { return y - x < 1.0e-6; }), times.end());

        CurveCursor gestPitch (n.gesture, 0.0f), gestTimbre (n.gestureTimbre, 0.0f), gestPress (n.gesturePress, 1.0f);
        for (const double t : times)
        {
            const float detune = (spread + offset + driftDepth * (float) std::sin (twoPi * (driftRate * t + driftPhase))) / 100.0f;
            float bend = detune + evalCurve (srcBend, t, 0.0f) + gestPitch.at (t);
            if (glideAmount != 0.0f && t < glideLen)
                bend += glideAmount * std::pow (1.0f - (float) (t / glideLen), exponent);
            if (vibrato && t > vibDelay)
            {
                const float ramp = std::clamp ((float) ((t - vibDelay) / vibFade), 0.0f, 1.0f);
                bend += vibDepth * ramp * (float) std::sin (twoPi * (vibRate * (t - vibDelay) + vibPhase));
            }
            n.bend.push_back ({ t, bend });

            const double effLen = std::max (len, 0.25);
            const float open = p.slideAmount * 0.7f * (1.0f - (float) std::exp (-3.0 * t / effLen));
            n.slide.push_back ({ t, srcSlide.empty() ? std::clamp (slideBase + open + gestTimbre.at (t), 0.0f, 1.0f)
                                                     : evalCurve (srcSlide, t, 0.5f) });

            const float x = (float) (t / len);
            float press = x < 0.6f ? pressStart + (pressPeak - pressStart) * (x / 0.6f)
                                   : pressPeak * (1.0f - 0.3f * (x - 0.6f) / 0.4f);
            press += p.breath * 0.15f * (float) std::sin (twoPi * (0.25 * (n.start + t) + breathPhase));
            n.pressure.push_back ({ t, srcPress.empty() ? std::clamp (press * gestPress.at (t), 0.0f, 1.0f)
                                                        : evalCurve (srcPress, t, 0.0f) });
        }

        n.releaseVelocity = std::clamp (0.3f + 0.4f * n.velocity, 0.0f, 1.0f);
    }
}

} // namespace dmpe

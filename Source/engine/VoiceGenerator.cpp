#include "VoiceGenerator.h"
#include "Rng.h"

#include <algorithm>
#include <cmath>

namespace dmpe
{
namespace
{

constexpr double twoPi = 6.283185307179586;

enum Salt : uint64_t { wordSalt = 101, pitchSalt, vowelSalt, exprSalt, typeSalt, velSalt };

enum class Sentence { statement, question, exclamation, close };

struct VoiceDef
{
    int minSyllables, maxSyllables; // per 16-step bar, at density 0 / 1
    int stressSteps;                // length of a stressed syllable (steps of a 16th grid)
    int recite;                     // reciting tone, scale steps above the chord root
    float breath;                   // part of the bar left silent after a sentence
    float slideBias;
    std::vector<float> vowelWeights; // U O E A I
};

const VoiceDef& voiceDef (VoiceStyle s)
{
    static const VoiceDef defs[] = {
        { 5, 11, 2, 4, 0.12f, 0.1f,   { 1, 3, 2, 3, 1 } },   // prophet
        { 3, 7,  2, 4, 0.16f, 0.35f,  { 2.5f, 3, 1.5f, 3, 0.5f } }, // lament
        { 2, 5,  3, 0, 0.1f,  0.2f,   { 2, 4, 0.5f, 4, 0.5f } }, // titan
        { 6, 13, 1, 2, 0.05f, -0.1f,  { 1, 1.5f, 2, 3.5f, 2 } }, // talkbox
    };
    return defs[(size_t) std::clamp ((int) s, 0, (int) VoiceStyle::count - 1)];
}

// Vowel brightness (roughly the second formant): U O E A I.
constexpr float vowelValue[] = { 0.08f, 0.3f, 0.62f, 0.78f, 0.97f };

struct Syllable
{
    int step = 0;   // onset in the bar
    int steps = 1;  // written length
    bool stress = false;
    bool wordEnd = false;
    bool last = false;
    int degree = 0; // scale steps above the chord root
    int octave = 0;

    // MIDI Learn: a syllable on a note of your lead keeps its pitch, length and velocity.
    bool learned = false;
    int semis = 0;
    double fixedLength = -1.0;
    float velocity = -1.0f;
};

int pickIndex (Rng& r, const std::vector<float>& w)
{
    float total = 0.0f;
    for (float x : w) total += x;
    float v = r.uniform() * total;
    for (size_t i = 0; i < w.size(); ++i)
    {
        v -= w[i];
        if (v <= 0.0f)
            return (int) i;
    }
    return (int) w.size() - 1;
}

// The words of one sentence: syllables with stress, laid on the grid, the last one lengthened.
std::vector<Syllable> sentenceRhythm (const GenParams& p, const VoiceDef& def, int n, Rng& r)
{
    const int beat = std::max (1, n / 4);
    const double per16 = def.minSyllables + (def.maxSyllables - def.minSyllables) * (double) p.density;
    const int breath = std::max (n >= 8 ? 1 : 0, (int) std::lround (def.breath * (float) n));
    const int available = std::max (1, n - breath);
    int count = std::clamp ((int) std::lround (per16 * n / 16.0), 1, available);

    // Words of 1..3 syllables; mostly stressed first (trochee), sometimes last (iamb).
    std::vector<Syllable> syl;
    while ((int) syl.size() < count)
    {
        const int w = std::min (count - (int) syl.size(), 1 + pickIndex (r, { 0.3f, 0.45f, 0.25f }));
        const int stressAt = w == 1 ? (r.chance (0.6f) ? 0 : -1) : (r.chance (0.7f) ? 0 : w - 1);
        for (int k = 0; k < w; ++k)
        {
            Syllable s;
            s.stress = k == stressAt;
            s.wordEnd = k == w - 1;
            s.steps = s.stress ? std::max (1, def.stressSteps * n / 16) : std::max (1, n / 16);
            if (s.stress && p.longNotes > 0.0f && r.chance (p.longNotes))
                s.steps += std::max (1, beat / 2);
            syl.push_back (s);
        }
    }
    syl.back().last = true;

    // Too long: stressed syllables shrink, then the sentence loses syllables.
    auto total = [&syl] { int t = 0; for (const auto& s : syl) t += s.steps; return t; };
    for (auto& s : syl)
        if (total() > available && s.steps > 1)
            s.steps = std::max (1, n / 16);
    while (total() > available && syl.size() > 1)
    {
        syl.pop_back();
        syl.back().last = syl.back().wordEnd = true;
    }
    for (auto& s : syl)
        if (total() > available)
            s.steps = 1;

    // Room left: a pickup rest, short pauses between words, and the last vowel held.
    int spare = available - total();
    int start = 0;
    if (spare > 1 && r.chance (0.35f))
    {
        start = std::min (spare / 2, std::max (1, beat / 2));
        spare -= start;
    }
    for (auto& s : syl)
        if (! s.last && s.wordEnd && spare > 1 && r.chance (0.3f))
        {
            s.steps += 1; // the pause is part of the slot; the note is cut before it
            spare -= 1;
        }
    syl.back().steps += spare;

    int at = start;
    for (auto& s : syl)
    {
        s.step = at;
        at += s.steps;
    }
    return syl;
}

// MIDI Learn: the syllables of one bar on the notes of your lead (your melody, spoken) or on the hits of your
// rhythm (the voice's own melody on your rhythm). Empty when you played nothing in that bar.
std::vector<Syllable> learnedSyllables (const std::vector<LearnedNote>& notes, int bar, int n, bool lead, Rng& r)
{
    std::vector<const LearnedNote*> mine;
    for (const auto& x : notes)
        if (x.bar == bar)
            mine.push_back (&x);
    std::vector<Syllable> syl;
    if (mine.empty())
        return syl;

    std::vector<float> velocities;
    for (const auto* x : mine)
        velocities.push_back (x->velocity);
    std::sort (velocities.begin(), velocities.end());
    const float median = velocities[velocities.size() / 2];
    const int beat = std::max (1, n / 4);
    const double stepLen = 4.0 / n;

    for (size_t i = 0; i < mine.size(); ++i)
    {
        const auto& x = *mine[i];
        const int next = i + 1 < mine.size() ? mine[i + 1]->step : n;
        Syllable s;
        s.step = x.step;
        s.steps = std::max (1, next - x.step);
        s.stress = x.step % beat == 0 || x.velocity >= std::max (0.75f, median + 0.05f);
        s.last = i + 1 == mine.size();
        if (lead)
        {
            s.learned = true;
            s.degree = x.degree;
            s.semis = x.semis;
            s.fixedLength = x.length;
            s.wordEnd = s.last || x.length < (s.steps - 0.5) * stepLen; // a rest after it ends the word
        }
        else
            s.wordEnd = s.last || s.steps >= std::max (2, n / 8) || r.chance (0.35f);
        s.velocity = x.velocity;
        syl.push_back (s);
    }
    return syl;
}

// Pitches of a sentence, in scale steps from the chord root.
void sentenceMelody (std::vector<Syllable>& syl, const GenParams& p, const VoiceDef& def, Sentence type, Rng& r)
{
    const int count = (int) syl.size();
    const int recite = def.recite;
    const float wander = 1.0f - std::clamp (p.pedal, 0.0f, 1.0f);
    const int cadence = std::min (count - 1, type == Sentence::question ? 2 : 3); // syllables of the ending

    for (int i = 0; i < count; ++i)
    {
        auto& s = syl[(size_t) i];
        const int fromEnd = count - 1 - i;
        switch (p.voice)
        {
            case VoiceStyle::prophet:
            case VoiceStyle::count:
                if (i == 0)
                    s.degree = r.chance (p.pedal) ? recite : recite - 1 - (r.chance (0.4f) ? 1 : 0);
                else if (fromEnd < cadence)
                {
                    switch (type)
                    {
                        case Sentence::question:    s.degree = recite + (cadence - fromEnd); break;
                        case Sentence::exclamation: s.degree = fromEnd == 1 ? recite + 3 : (fromEnd == 0 ? recite : recite + 1); break;
                        case Sentence::statement:   s.degree = fromEnd == 0 ? 0 : std::max (1, recite - (cadence - fromEnd)); break;
                        case Sentence::close:       s.degree = fromEnd == 0 ? 0 : fromEnd; break;
                    }
                }
                else
                {
                    s.degree = recite;
                    if (s.stress && r.chance (0.6f * wander))
                        s.degree += 1;
                    else if (! s.stress && r.chance (0.3f * wander))
                        s.degree -= 1;
                }
                break;

            case VoiceStyle::lament:
            {
                // Sighs: each stressed syllable falls a step onto the next; the line sinks word by word.
                if (i == 0)
                    s.degree = recite + (r.chance (0.5f) ? 1 : 0);
                else
                {
                    const auto& prev = syl[(size_t) i - 1];
                    s.degree = prev.stress ? prev.degree - 1 : (prev.wordEnd && r.chance (0.5f + 0.4f * wander) ? prev.degree + 1 : prev.degree);
                }
                if (s.last)
                    s.degree = type == Sentence::question ? 1 : (type == Sentence::exclamation ? recite + 1 : 0);
                break;
            }

            case VoiceStyle::titan:
            {
                // Low call, a leap up on the first stress (fifth, or octave), then steps back down.
                const int peak = r.chance (0.35f + 0.6f * p.octave) ? 7 : 4;
                if (i == 0)
                    s.degree = s.stress ? peak : 0;
                else
                {
                    const auto& prev = syl[(size_t) i - 1];
                    const bool leapDone = std::any_of (syl.begin(), syl.begin() + i, [] (const Syllable& x) { return x.degree >= 4; });
                    s.degree = ! leapDone && s.stress ? peak : std::max (0, prev.degree - (r.chance (0.6f) ? 1 : 0));
                }
                if (s.last)
                    s.degree = type == Sentence::close || type == Sentence::statement ? 0
                             : (type == Sentence::question ? 5 : 7);
                break;
            }

            case VoiceStyle::talkbox:
            {
                static const std::vector<int> tones { 0, 2, 4, 7, 1, -1 };
                static const std::vector<float> w { 4, 3, 3, 1.5f, 1, 1 };
                if (i > 0 && r.chance (0.5f * p.pedal))
                    s.degree = syl[(size_t) i - 1].degree; // repeated note: the words carry it
                else
                    s.degree = tones[(size_t) pickIndex (r, w)];
                if (s.stress && s.degree % 2 != 0)
                    s.degree -= 1; // stress on chord tones
                if (s.last)
                    s.degree = type == Sentence::question ? 4 : (type == Sentence::exclamation ? 7 : 0);
                break;
            }
        }

        // Outbursts: a stressed syllable jumps up (Titan does it in its own way).
        if (p.voice != VoiceStyle::titan && s.stress && ! s.last && r.chance (0.35f * p.octave))
            s.degree += 3;
    }
}

float smoothStep (float x)
{
    x = std::clamp (x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// The expression of one syllable. `slideFrom`: semitones to glide in from (0 = none).
void shapeSyllable (Note& n, const Syllable& s, Sentence type, float slideFrom, bool legatoOut, const GenParams& p,
                    const ExprParams& e, const VoiceDef& def, Rng& r)
{
    const double len = std::max (0.02, n.length);

    // ---- vowel (CC74): consonant onset, vowel, diphthong, closing mouth
    const int vowel = pickIndex (r, def.vowelWeights);
    const int consonant = pickIndex (r, { 1.0f, 2.0f, 2.0f, 0.7f, 1.2f }); // none, m/b/w, d/t/n, y, r/l
    const bool diphthong = r.chance (p.voice == VoiceStyle::talkbox ? 0.6f : 0.35f) && len > 0.3;
    static const int glideTo[] = { 1, 0, 4, 4, 2 }; // U->O, O->U, E->I, A->I, I->E
    const int vowel2 = diphthong ? (vowel == 3 && r.chance (0.5f) ? 0 : glideTo[vowel]) : vowel;
    const bool closeMouth = s.wordEnd && ! legatoOut && r.chance (0.6f);

    const float depth = 1.3f * std::clamp (p.vowels, 0.0f, 1.0f);
    const float centre = std::clamp (e.slideAmount, 0.0f, 1.0f);
    auto vowelAt = [&] (float v) { return std::clamp (centre + (v - 0.55f) * depth, 0.0f, 1.0f); };
    const float target = vowelAt (vowelValue[vowel]);
    const float target2 = vowelAt (vowelValue[vowel2]);
    float onsetValue = target;
    double onsetTime = 0.03;
    switch (consonant)
    {
        case 1: onsetValue = vowelAt (0.0f);  onsetTime = 0.07; break; // m / b / w: closed lips opening
        case 2: onsetValue = vowelAt (0.45f); onsetTime = 0.025; break; // d / t / n: a flick
        case 3: onsetValue = vowelAt (1.0f);  onsetTime = 0.1;  break; // y: from I into the vowel
        case 4: onsetValue = vowelAt (0.35f); onsetTime = 0.06; break; // r / l
        default: break;
    }
    onsetTime = std::min (onsetTime, len * 0.4);

    // ---- loudness (pressure)
    const float amount = 0.55f + 0.45f * std::clamp (e.pressureAmount, 0.0f, 1.0f);
    const float peak = (s.stress ? 0.95f : 0.72f) * amount;
    const double attack = std::min (len * 0.3, consonant == 2 ? 0.02 : (consonant == 0 ? 0.05 : 0.035));
    const float endLevel = legatoOut ? (s.wordEnd ? 0.45f : 0.72f) * peak : 0.05f;
    const bool epicTail = s.last && (type == Sentence::close || p.voice == VoiceStyle::titan);

    // ---- pitch (bend)
    const float inflect = std::clamp (p.inflection, 0.0f, 1.0f);
    const float scoop = slideFrom != 0.0f ? 0.0f : inflect * (s.stress ? 0.9f : 0.35f) * (0.7f + 0.6f * r.uniform());
    const double scoopTime = 0.05 + 0.04 * r.uniform();
    const double glideTime = std::max (0.03, std::min ((double) e.glideTime * 1.3, len * 0.6));
    const float growl = std::clamp (p.growl, 0.0f, 1.0f) * (s.stress ? 1.0f : 0.6f);
    const double growlPhase = r.uniform();
    const bool vibrato = e.vibratoDepth > 0.0f && len > std::max (0.3, (double) e.vibratoDelay * 0.6);
    const double vibDelay = std::min ((double) e.vibratoDelay * 0.6, len * 0.4);
    const float vibDepth = e.vibratoDepth * (epicTail ? 1.4f : 1.0f);
    const double vibPhase = r.uniform();

    float endBend = 0.0f;  // reached at the end of the note
    double endFrom = 1.0;  // from this fraction of the note
    if (s.last)
        switch (type)
        {
            case Sentence::statement:   endBend = -inflect * (1.2f + r.uniform()); endFrom = 0.7; break;
            case Sentence::question:    endBend = inflect * (1.0f + r.uniform());  endFrom = 0.6; break;
            case Sentence::exclamation: endBend = -inflect * 1.5f;                 endFrom = 0.75; break;
            case Sentence::close:       endBend = -inflect * (p.voice == VoiceStyle::titan ? 9.0f : 4.0f); endFrom = 0.72; break;
        }
    else if (s.wordEnd && ! legatoOut)
    {
        endBend = -inflect * 0.4f;
        endFrom = 0.75;
    }
    if (p.voice == VoiceStyle::titan && s.last && type != Sentence::question)
        endBend = std::min (endBend, -inflect * 7.0f);

    n.bend.clear();
    n.slide.clear();
    n.pressure.clear();
    n.lockedExpr = true;

    for (double t = 0.0;;)
    {
        const float x = (float) (t / len);

        // pitch
        float bend = 0.0f;
        if (slideFrom != 0.0f && t < glideTime)
            bend += slideFrom * std::pow (1.0f - (float) (t / glideTime), 2.2f);
        if (scoop > 0.0f && t < scoopTime)
            bend -= scoop * (1.0f - smoothStep ((float) (t / scoopTime)));
        if (growl > 0.0f && t < 0.16)
        {
            const float fade = 1.0f - (float) (t / 0.16);
            bend += growl * 0.45f * fade * (float) (std::sin (twoPi * (14.0 * t + growlPhase)) + 0.5 * std::sin (twoPi * (23.0 * t + 0.3)));
        }
        if (vibrato && t > vibDelay)
        {
            const float ramp = std::clamp ((float) ((t - vibDelay) / 0.4), 0.0f, 1.0f);
            bend += vibDepth * ramp * (float) std::sin (twoPi * (e.vibratoRate * (t - vibDelay) + vibPhase));
        }
        if (endBend != 0.0f && x > endFrom)
        {
            const float y = (float) ((x - endFrom) / (1.0 - endFrom));
            bend += endBend * (endBend < 0.0f ? y * y : smoothStep (y));
        }
        n.bend.push_back ({ t, bend });

        // vowel
        float v = target;
        if (t < onsetTime)
            v = onsetValue + (target - onsetValue) * smoothStep ((float) (t / onsetTime));
        if (diphthong && x > 0.55f)
            v = target + (target2 - target) * smoothStep ((x - 0.55f) / 0.4f);
        if (closeMouth && t > len - 0.07)
            v += (vowelAt (0.0f) - v) * smoothStep ((float) ((t - (len - 0.07)) / 0.07));
        n.slide.push_back ({ t, std::clamp (v, 0.0f, 1.0f) });

        // loudness
        float press;
        if (t < attack)
            press = peak * (0.35f + 0.65f * (float) (t / attack));
        else if (epicTail)
            press = x < 0.55f ? peak * (0.85f + 0.15f * x / 0.55f) : peak * (1.0f - 0.9f * smoothStep ((x - 0.55f) / 0.45f));
        else
        {
            const float body = peak * (1.0f - 0.2f * smoothStep (x / 0.8f));
            press = x < 0.8f ? body : body + (endLevel - body) * smoothStep ((x - 0.8f) / 0.2f);
        }
        if (growl > 0.0f && t < 0.16)
            press += growl * 0.2f * (1.0f - (float) (t / 0.16)) * (float) std::sin (twoPi * 17.0 * t);
        press += e.breath * 0.06f * (float) std::sin (twoPi * (0.25 * (n.start + t)));
        n.pressure.push_back ({ t, std::clamp (press, 0.0f, 1.0f) });

        if (t >= len)
            break;
        double step = t < 0.18 ? 1.0 / 96.0 : 1.0 / 32.0;
        if (vibrato && t > vibDelay)
            step = std::min (step, 1.0 / (16.0 * std::max (0.25f, e.vibratoRate)));
        if (endBend != 0.0f && x > endFrom - 0.05)
            step = std::min (step, 1.0 / 48.0);
        t = std::min (len, t + step);
    }
    n.releaseVelocity = legatoOut ? 0.5f : 0.3f;
}

} // namespace

Phrase generateVoice (const GenParams& p, const ExprParams& e)
{
    const auto& def = voiceDef (p.voice);
    const uint64_t seed = (uint64_t) p.seed * 977ull + (uint64_t) p.voice * 31ull + 7ull;
    const int bars = std::clamp (p.bars, 1, 16);
    const int n = stepsPerBar (p.rate);
    const double stepLen = 4.0 / n;
    const auto harmony = leadHarmony (p);
    const auto sections = formSections (p.form, bars);

    const int tonic = p.key + 12 * (p.baseOctave + 1);
    const int lowest = tonic - 5;
    const int highest = tonic + 12 * std::max (1, p.rangeOctaves) + 7;

    struct Placed { Note note; Syllable syl; Sentence type; uint64_t key; };
    std::vector<Placed> placed;

    // MIDI Learn: your lead is spoken as it is; on your rhythm the voice says its own sentences.
    const bool learnedLead = p.motif == MotifSource::lead && p.learned != nullptr && ! p.learned->lead.empty();
    const bool learnedRhythm = p.motif == MotifSource::rhythm && p.learned != nullptr && ! p.learned->rhythm.empty();
    const auto learnedList = learnedLead || learnedRhythm ? learnedNotes (p, learnedRhythm) : std::vector<LearnedNote> {};

    for (int bar = 0; bar < bars; ++bar)
    {
        // The sentence: the same label says the same words (MUTATE re-writes everything but A);
        // without a form every bar speaks, the odd bars answer.
        Sentence type = Sentence::statement;
        uint64_t key = 0;
        if (! sections.empty())
        {
            const auto& sec = sections[(size_t) bar];
            switch (sec.kind)
            {
                case SectionKind::statement:    type = Sentence::statement; break;
                case SectionKind::answer:       type = Sentence::question; break;
                case SectionKind::closedAnswer: type = Sentence::statement; break;
                case SectionKind::close:        type = Sentence::close; break;
                case SectionKind::fragment:     type = Sentence::exclamation; break;
            }
            const bool isA = sec.kind == SectionKind::statement && sec.shift == 0;
            key = hashLabel (sec.label.c_str()) * 3ull + (isA ? 0ull : (uint64_t) p.variation * 7919ull);
        }
        else
        {
            type = bar % 4 == 3 ? Sentence::close
                 : (bar % 2 == 1 ? (p.voice == VoiceStyle::titan ? Sentence::exclamation : Sentence::question) : Sentence::statement);
            key = (uint64_t) bar * 2654435761ull + (bar % 2 == 1 ? (uint64_t) p.variation * 7919ull : 0ull);
        }

        auto rhythmRng = keyedRng ({ seed, wordSalt, key });
        auto syl = learnedLead || learnedRhythm ? learnedSyllables (learnedList, bar % 4, n, learnedLead, rhythmRng)
                                                : sentenceRhythm (p, def, n, rhythmRng);
        if (! learnedLead)
        {
            auto pitchRng = keyedRng ({ seed, pitchSalt, key });
            if (! syl.empty())
                sentenceMelody (syl, p, def, type, pitchRng);
            if (! sections.empty() && sections[(size_t) bar].kind == SectionKind::statement)
                for (auto& s : syl)
                    s.degree += sections[(size_t) bar].shift; // A', A+, A++: the sentence said higher
        }

        for (size_t i = 0; i < syl.size(); ++i)
        {
            const auto& s = syl[i];
            Note note;
            note.start = bar * 4.0 + s.step * stepLen;
            int pitch = harmony.pitch (note.start, tonic, s.degree) + 12 * s.octave;
            if (s.semis != 0 && (! p.scaleLock || scales::inScale (pitch + s.semis, p.key, p.scale)))
                pitch += s.semis; // a chromatic note of your lead
            if (! s.learned) // your lead keeps its register
            {
                while (pitch > highest) pitch -= 12;
                while (pitch < lowest)  pitch += 12;
            }
            note.pitch = std::clamp (pitch, 0, 127);
            note.accent = s.stress;
            const float jitter = keyedRng ({ seed, velSalt, key, (uint64_t) i }).uniform();
            note.velocity = s.velocity >= 0.0f ? std::clamp (s.velocity, 0.05f, 1.0f)
                                               : std::clamp ((s.stress ? 0.88f : 0.7f) + (s.last ? 0.04f : 0.0f) + 0.06f * jitter, 0.05f, 1.0f);
            placed.push_back ({ note, s, type, key });
        }
    }

    // Lengths: inside a word the syllables are tied (legato), between words the gate shortens them, the
    // last vowel of a sentence sounds its whole slot.
    Phrase out;
    out.lengthBeats = bars * 4.0;
    const float slideProb = std::clamp (p.slide + def.slideBias, 0.0f, 1.0f);
    for (size_t i = 0; i < placed.size(); ++i)
    {
        auto& cur = placed[i];
        const double slot = cur.syl.steps * stepLen;
        const bool hasNext = i + 1 < placed.size();
        const double nextStart = hasNext ? placed[i + 1].note.start : out.lengthBeats;
        const bool contiguous = hasNext && std::abs (nextStart - (cur.note.start + slot)) < 1.0e-9;
        const bool inWord = contiguous && ! cur.syl.wordEnd;
        if (cur.syl.fixedLength > 0.0) // your lead: your articulation, tied where you played legato
            cur.note.length = contiguous && cur.syl.fixedLength >= slot - 0.02 ? nextStart - cur.note.start
                                                                                : std::min (cur.syl.fixedLength, slot);
        else if (inWord)
            cur.note.length = nextStart - cur.note.start;
        else if (cur.syl.last)
            cur.note.length = slot - std::min (0.1, slot * 0.1);
        else
            cur.note.length = slot * (0.6 + 0.35 * std::clamp (p.gate, 0.0f, 1.0f));
        cur.note.length = std::max (0.05, std::min (cur.note.length, out.lengthBeats - cur.note.start - 1.0e-3));
    }

    for (size_t i = 0; i < placed.size(); ++i)
    {
        auto& cur = placed[i];
        // Portamento from the previous syllable when it is tied into this one.
        float slideFrom = 0.0f;
        if (i > 0)
        {
            const auto& prev = placed[i - 1];
            auto r = keyedRng ({ seed, exprSalt, prev.key, (uint64_t) cur.syl.step, 1ull });
            const bool tied = std::abs (prev.note.end() - cur.note.start) < 1.0e-9;
            if (tied && prev.note.pitch != cur.note.pitch && r.chance (slideProb))
            {
                slideFrom = (float) std::clamp (prev.note.pitch - cur.note.pitch, -24, 24);
                cur.note.glideFrom = prev.note.pitch;
            }
        }
        const bool legatoOut = i + 1 < placed.size() && std::abs (cur.note.end() - placed[i + 1].note.start) < 1.0e-9;
        auto r = keyedRng ({ seed, vowelSalt, cur.key, (uint64_t) cur.syl.step });
        shapeSyllable (cur.note, cur.syl, cur.type, slideFrom, legatoOut, p, e, def, r);
        out.notes.push_back (cur.note);
    }
    out.sortByStart();
    return out;
}

Phrase generateLead (const GenParams& p, const ExprParams& e)
{
    return p.engine == LeadEngine::voice ? generateVoice (p, e) : generateMelody (p);
}

} // namespace dmpe

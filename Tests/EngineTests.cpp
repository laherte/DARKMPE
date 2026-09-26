#include <juce_audio_basics/juce_audio_basics.h>

#include "engine/CinematicEngine.h"
#include "engine/HarmonyEngine.h"
#include "engine/Humanize.h"
#include "engine/KitGenerator.h"
#include "engine/ExpressionShaper.h"
#include "engine/MelodyGenerator.h"
#include "engine/MidiFileIO.h"
#include "engine/MpeRenderer.h"
#include "engine/VoicingEngine.h"
#include "engine/VoiceGenerator.h"
#include "engine/LeadHarmony.h"
#include "engine/MidiLearn.h"

#include <map>
#include <tuple>
#include <set>

using namespace dmpe;

namespace
{

// Checks that no two sounding notes share a member channel and every note-on has a note-off.
bool channelsAreExclusive (const juce::MidiMessageSequence& seq, juce::String& why)
{
    std::map<int, int> sounding; // channel -> pitch
    for (auto* ev : seq)
    {
        const auto& m = ev->message;
        if (m.isNoteOn())
        {
            if (sounding.count (m.getChannel()) != 0)
            {
                why = "channel " + juce::String (m.getChannel()) + " reused at " + juce::String (m.getTimeStamp());
                return false;
            }
            sounding[m.getChannel()] = m.getNoteNumber();
        }
        else if (m.isNoteOff())
        {
            auto it = sounding.find (m.getChannel());
            if (it == sounding.end() || it->second != m.getNoteNumber())
            {
                why = "unmatched note-off";
                return false;
            }
            sounding.erase (it);
        }
    }
    if (! sounding.empty())
    {
        why = "hanging notes";
        return false;
    }
    return true;
}

Phrase chordProgression()
{
    // Am - F - G - Em, one bar each
    const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 }, { 52, 55, 59 } };
    Phrase p;
    p.lengthBeats = 16.0;
    for (int c = 0; c < 4; ++c)
        for (int pitch : chords[c])
        {
            Note n;
            n.start = c * 4.0;
            n.length = 4.0;
            n.pitch = pitch;
            p.notes.push_back (n);
        }
    return p;
}

// A hand-played MPE chord: A2 doubled on two channels (unison), C4, E4, each with its own expression.
juce::MidiMessageSequence playedMpeChord()
{
    juce::MidiMessageSequence seq;
    for (const auto meta : juce::MPEMessages::setLowerZone (15, 48, 2))
        seq.addEvent (meta.getMessage(), 0.0);

    auto on = [&] (int ch, int pitch, double t) { seq.addEvent (juce::MidiMessage::noteOn (ch, pitch, (juce::uint8) 100), t); };
    auto off = [&] (int ch, int pitch, double t) { seq.addEvent (juce::MidiMessage::noteOff (ch, pitch), t); };

    // expression before note-on (MPE spec order)
    for (int ch = 2; ch <= 5; ++ch)
        seq.addEvent (juce::MidiMessage::pitchWheel (ch, 8192), 0.0);
    on (2, 45, 0.0);
    on (3, 45, 0.02);   // unison on another channel
    on (4, 60, 0.05);   // rolled chord
    on (5, 64, 0.08);

    for (int i = 1; i <= 16; ++i)
    {
        const double t = i * 0.125;
        seq.addEvent (juce::MidiMessage::pitchWheel (2, 8192 + (int) (i / 16.0 * 2.0 / 48.0 * 8192.0)), t); // slides up 2 st
        seq.addEvent (juce::MidiMessage::controllerEvent (4, 74, 20 + i * 6), t);
        seq.addEvent (juce::MidiMessage::channelPressureChange (5, i * 7), t);
    }

    for (auto [ch, pitch] : { std::pair { 2, 45 }, { 3, 45 }, { 4, 60 }, { 5, 64 } })
        off (ch, pitch, 4.0);

    // second chord (F), plain
    on (2, 41, 4.0); on (3, 57, 4.0); on (4, 60, 4.0);
    off (2, 41, 8.0); off (3, 57, 8.0); off (4, 60, 8.0);
    return seq;
}

Phrase loadFromSequence (const juce::MidiMessageSequence& beatSeq, LoadInfo& info)
{
    const auto mf = makeMidiFile (beatSeq, 120.0, "test");
    juce::MemoryOutputStream mos;
    mf.writeTo (mos, 1);
    juce::MemoryInputStream mis (mos.getData(), mos.getDataSize(), false);
    Phrase p;
    juce::String err;
    loadMidiStream (mis, p, err, &info);
    return p;
}

} // namespace

class EngineTests : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("DarkMPE engine") {}

    void runTest() override
    {
        beginTest ("Generator is deterministic and stays in range");
        for (int style = 0; style < (int) Style::count; ++style)
        {
            GenParams gp;
            gp.style = (Style) style;
            gp.seed = 42 + style;
            gp.bars = 8;
            gp.chroma = 0.3f;
            const auto a = generateMelody (gp);
            const auto b = generateMelody (gp);

            expect (! a.empty(), "empty phrase");
            expectEquals ((int) a.notes.size(), (int) b.notes.size());
            bool same = true;
            for (size_t i = 0; i < a.notes.size(); ++i)
                same = same && a.notes[i].pitch == b.notes[i].pitch && a.notes[i].start == b.notes[i].start;
            expect (same, "same seed must give the same phrase");

            for (const auto& n : a.notes)
            {
                expect (n.chromatic || scales::inScale (n.pitch, gp.key, gp.scale),
                        "out-of-scale note " + juce::String (n.pitch));
                expect (n.start >= 0.0 && n.end() <= a.lengthBeats + 1.0e-6, "note outside phrase");
                expect (n.length > 0.0);
            }

            gp.seed += 1000;
            const auto c = generateMelody (gp);
            bool differs = c.notes.size() != a.notes.size();
            for (size_t i = 0; ! differs && i < a.notes.size(); ++i)
                differs = a.notes[i].pitch != c.notes[i].pitch || a.notes[i].start != c.notes[i].start;
            expect (differs, "different seeds should differ");
        }

        beginTest ("Every style stays in every scale (5-note scales included)");
        for (int style = 0; style < (int) Style::count; ++style)
            for (int scale = 0; scale < (int) scales::Scale::count; ++scale)
            {
                GenParams gp;
                gp.style = (Style) style;
                gp.scale = (scales::Scale) scale;
                gp.seed = 7 + style * 13 + scale;
                gp.bars = 8;
                const auto mel = generateMelody (gp);
                const juce::String what = juce::String (styleNames[style]) + " / " + scales::scaleNames[scale];
                expect (! mel.empty(), what);
                for (const auto& n : mel.notes)
                    expect (n.chromatic || scales::inScale (n.pitch, gp.key, gp.scale), what + ": out-of-scale note");
                expect (isMonophonic (mel), what + " must stay a single line");
            }

        beginTest ("Gallop plays 8th + two 16ths");
        {
            GenParams gp;
            gp.style = Style::gallop;
            gp.density = 1.0f;
            const auto mel = generateMelody (gp);
            std::set<int> steps;
            for (const auto& n : mel.notes)
                if (n.start < 4.0)
                    steps.insert ((int) std::lround (n.start * 4.0));
            expect (steps == std::set<int> { 0, 2, 3, 4, 6, 7, 8, 10, 11, 12, 14, 15 }, "gallop rhythm");
        }

        beginTest ("Humanize is deterministic, keeps glide ties and stays in the loop");
        {
            GenParams gp;
            gp.slide = 0.8f;
            gp.bars = 8;
            const auto plain = generateMelody (gp);
            auto zero = plain;
            humanize (zero, 0.0f, 3);
            bool same = true;
            for (size_t i = 0; i < plain.notes.size(); ++i)
                same = same && zero.notes[i].start == plain.notes[i].start && zero.notes[i].velocity == plain.notes[i].velocity;
            expect (same, "amount 0 must not change anything");

            auto a = plain, b = plain;
            humanize (a, 1.0f, 3);
            humanize (b, 1.0f, 3);
            int moved = 0;
            for (size_t i = 0; i < a.notes.size(); ++i)
            {
                expect (a.notes[i].start == b.notes[i].start && a.notes[i].velocity == b.notes[i].velocity, "deterministic");
                expect (a.notes[i].start >= 0.0 && a.notes[i].end() <= a.lengthBeats + 1.0e-6, "note outside phrase");
                moved += std::abs (a.notes[i].start - plain.notes[i].start) > 1.0e-9 ? 1 : 0;
            }
            expectGreaterThan (moved, 0);
            for (size_t i = 1; i < a.notes.size(); ++i)
                if (a.notes[i].glideFrom >= 0)
                    expect (std::abs (a.notes[i - 1].end() - a.notes[i].start) < 1.0e-9, "glide tie broken");
        }

        beginTest ("Monophonic lead renders to exclusive MPE channels with bends in range");
        {
            GenParams gp;
            gp.slide = 0.8f;
            gp.bars = 4;
            auto phrase = generateMelody (gp);
            shapeExpression (phrase, {});
            const auto seq = renderMpe (phrase);

            juce::String why;
            expect (channelsAreExclusive (seq, why), why);

            int bends = 0, slides = 0, pressure = 0;
            for (auto* ev : seq)
            {
                const auto& m = ev->message;
                if (m.isNoteOnOrOff())
                    expect (m.getChannel() >= 2 && m.getChannel() <= 16, "note on master channel");
                if (m.isPitchWheel()) ++bends;
                if (m.isController() && m.getControllerNumber() == 74) ++slides;
                if (m.isChannelPressure()) ++pressure;
            }
            expect (bends > 0 && slides > 0 && pressure > 0, "missing MPE expression");
        }

        beginTest ("Glide starts at the previous pitch");
        {
            Phrase p;
            p.lengthBeats = 4.0;
            Note a; a.start = 0.0; a.length = 1.0; a.pitch = 57;
            Note b; b.start = 1.0; b.length = 1.0; b.pitch = 64; b.glideFrom = 57;
            p.notes = { a, b };
            ExprParams ep;
            ep.detuneCents = 0.0f;
            ep.vibratoDepth = 0.0f;
            shapeExpression (p, ep);
            expectWithinAbsoluteError (evalCurve (p.notes[1].bend, 0.0, 99.0f), -7.0f, 0.05f);
            expectWithinAbsoluteError (evalCurve (p.notes[1].bend, 0.9, 99.0f), 0.0f, 0.05f);
            expectEquals (bendToMidi (-7.0f, 48), 8192 - (int) std::lround (7.0 / 48.0 * 8192.0));
        }

        beginTest ("Voicing: chord detection, voice count, range and minimal motion");
        {
            const auto prog = chordProgression();
            const auto chords = detectChords (prog);
            expectEquals ((int) chords.size(), 4);
            expect (! isMonophonic (prog));

            for (int mode = 0; mode < (int) VoicingMode::count; ++mode)
            {
                VoicingParams vp;
                vp.mode = (VoicingMode) mode;
                vp.voices = 5;
                vp.strum = 0.03;
                auto voiced = applyVoicing (prog, vp);
                expect (! voiced.empty());

                for (const auto& n : voiced.notes)
                {
                    if (vp.mode != VoicingMode::asPlayed)
                        expect (n.pitch >= vp.lowPitch && n.pitch <= vp.highPitch,
                                juce::String (voicingNames[mode]) + " out of range: " + juce::String (n.pitch));
                    expect (n.glideFrom < 0 || std::abs (n.glideFrom - n.pitch) <= 48);
                }

                if (vp.mode != VoicingMode::asPlayed)
                    expectEquals ((int) voiced.notes.size(), 4 * vp.voices, voicingNames[mode]);

                shapeExpression (voiced, {});
                juce::String why;
                expect (channelsAreExclusive (renderMpe (voiced), why), juce::String (voicingNames[mode]) + ": " + why);
            }

            // With voice leading each upper voice should move by at most a fourth between these chords.
            VoicingParams vp;
            vp.mode = VoicingMode::openSpread;
            vp.voices = 4;
            vp.voiceLeading = true;
            const auto voiced = applyVoicing (prog, vp);
            for (const auto& n : voiced.notes)
                if (n.glideFrom >= 0 && n.voiceIndex > 0)
                    expect (std::abs (n.glideFrom - n.pitch) <= 5, "voice leap " + juce::String (n.glideFrom) + "->" + juce::String (n.pitch));
        }

        beginTest ("Melody input gets unison stack with per-voice detune");
        {
            Phrase mel;
            mel.lengthBeats = 4.0;
            for (int i = 0; i < 4; ++i)
            {
                Note n; n.start = i; n.length = 1.0; n.pitch = 60 + i * 2;
                mel.notes.push_back (n);
            }
            expect (isMonophonic (mel));
            VoicingParams vp;
            vp.mode = VoicingMode::unisonStack;
            vp.voices = 3;
            auto out = applyVoicing (mel, vp);
            expectEquals ((int) out.notes.size(), 12);
            ExprParams ep;
            ep.detuneCents = 20.0f;
            shapeExpression (out, ep);
            float lo = 1.0f, hi = -1.0f;
            for (const auto& n : out.notes)
                if (n.start == 0.0)
                {
                    lo = std::min (lo, n.bend.front().v);
                    hi = std::max (hi, n.bend.front().v);
                }
            expect (hi - lo > 0.2f, "unison voices should be detuned");
        }

        beginTest ("MIDI file roundtrip keeps notes and MPE channels");
        {
            GenParams gp;
            auto phrase = generateMelody (gp);
            shapeExpression (phrase, {});
            const auto seq = renderMpe (phrase);
            const auto mf = makeMidiFile (seq, 128.0, "DarkMPE");

            juce::MemoryOutputStream mos;
            expect (mf.writeTo (mos, 1));

            juce::MemoryInputStream mis (mos.getData(), mos.getDataSize(), false);
            Phrase back;
            juce::String err;
            expect (loadMidiStream (mis, back, err), err);
            expectEquals ((int) back.notes.size(), (int) phrase.notes.size());
            for (size_t i = 0; i < std::min (back.notes.size(), phrase.notes.size()); ++i)
            {
                expectEquals (back.notes[i].pitch, phrase.notes[i].pitch);
                expectWithinAbsoluteError (back.notes[i].start, phrase.notes[i].start, 1.0 / filePpq);
            }

            juce::MidiFile reread;
            juce::MemoryInputStream mis2 (mos.getData(), mos.getDataSize(), false);
            expect (reread.readFrom (mis2));
            juce::MidiMessageSequence all;
            for (int t = 0; t < reread.getNumTracks(); ++t)
                all.addSequence (*reread.getTrack (t), 0.0);
            juce::String why;
            juce::MidiMessageSequence notesOnly;
            for (auto* ev : all)
                if (ev->message.isNoteOnOrOff())
                    notesOnly.addEvent (ev->message);
            expect (channelsAreExclusive (notesOnly, why), why);
        }
    }
};

class MpeImportTests : public juce::UnitTest
{
public:
    MpeImportTests() : juce::UnitTest ("DarkMPE MPE import") {}

    void runTest() override
    {
        beginTest ("Polyphonic MPE file: unisons survive, expression becomes per-note curves");
        LoadInfo info;
        const auto src = loadFromSequence (playedMpeChord(), info);
        expect (info.mpe, "file should be detected as MPE");
        expectEquals ((int) src.notes.size(), 7);
        int unisonA = 0;
        for (const auto& n : src.notes)
        {
            expect (n.length > 3.8, "note too short: " + juce::String (n.pitch) + " len " + juce::String (n.length));
            if (n.pitch == 45)
            {
                ++unisonA;
                if (! n.bend.empty())
                    expectWithinAbsoluteError (n.bend.back().v, 2.0f, 0.05f);
            }
        }
        expectEquals (unisonA, 2);
        expectEquals (info.notesWithExpression, 3);
        expect (! isMonophonic (src));

        beginTest ("As Played keeps the performance, including expression");
        {
            VoicingParams vp;
            vp.mode = VoicingMode::asPlayed;
            auto out = applyVoicing (src, vp);
            expectEquals ((int) out.notes.size(), 7);
            ExprParams ep;
            ep.detuneCents = 0.0f;
            shapeExpression (out, ep);
            float maxBend = 0.0f, maxSlideDelta = 0.0f;
            for (const auto& n : out.notes)
                if (n.start < 1.0)
                {
                    for (const auto& pt : n.bend) maxBend = std::max (maxBend, pt.v);
                    if (n.pitch == 60)
                        maxSlideDelta = n.slide.back().v - n.slide.front().v;
                }
            expectWithinAbsoluteError (maxBend, 2.0f, 0.05f);
            expect (maxSlideDelta > 0.5f, "CC74 ramp should be kept");
            juce::String why;
            expect (channelsAreExclusive (renderMpe (out), why), why);

            ep.keepInput = false;
            auto gen = applyVoicing (src, vp);
            shapeExpression (gen, ep);
            float genMax = 0.0f;
            for (const auto& n : gen.notes)
                if (n.pitch == 45 && n.start < 1.0)
                    for (const auto& pt : n.bend) genMax = std::max (genMax, pt.v);
            expect (genMax < 1.0f, "keepInput = false must replace the player's slide");
        }

        beginTest ("Revoiced chords inherit the player's expression");
        {
            VoicingParams vp;
            vp.mode = VoicingMode::openSpread;
            vp.voices = 5;
            auto out = applyVoicing (src, vp);
            int inherited = 0;
            for (const auto& n : out.notes)
                inherited += (n.start < 1.0 && n.hasSourceExpr) ? 1 : 0;
            expectGreaterThan (inherited, 2);
            shapeExpression (out, {});
            juce::String why;
            expect (channelsAreExclusive (renderMpe (out), why), why);
        }

        beginTest ("Legato MPE lead with overlapping notes is a melody");
        {
            juce::MidiMessageSequence seq;
            const int pitches[] = { 57, 60, 64, 62, 60, 57, 55, 57 };
            for (int i = 0; i < 8; ++i)
            {
                const int ch = 2 + (i % 4);
                seq.addEvent (juce::MidiMessage::noteOn (ch, pitches[i], (juce::uint8) 90), i * 0.5);
                seq.addEvent (juce::MidiMessage::noteOff (ch, pitches[i]), i * 0.5 + 0.6); // 0.1 beat overlap
            }
            LoadInfo li;
            const auto lead = loadFromSequence (seq, li);
            expect (li.mpe);
            expect (isMonophonic (lead), "legato overlaps must not turn a lead into chords");
            VoicingParams vp;
            vp.mode = VoicingMode::unisonStack;
            vp.voices = 3;
            expectEquals ((int) applyVoicing (lead, vp).notes.size(), 24);
        }

        beginTest ("Plain MIDI pitch bend decodes with a +-2 range");
        {
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0.0);
            seq.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 1.0);
            seq.addEvent (juce::MidiMessage::noteOff (1, 60), 2.0);
            LoadInfo li;
            const auto p = loadFromSequence (seq, li);
            expect (! li.mpe);
            expectEquals ((int) p.notes.size(), 1);
            expectWithinAbsoluteError (p.notes[0].bend.back().v, 2.0f, 0.01f);
        }

        beginTest ("Rendered MPE output re-imports with the same expression");
        {
            VoicingParams vp;
            vp.mode = VoicingMode::drop2;
            auto voiced = applyVoicing (chordProgressionForImport(), vp);
            ExprParams ep;
            ep.keepInput = false;
            shapeExpression (voiced, ep);
            LoadInfo li;
            const auto back = loadFromSequence (renderMpe (voiced), li);
            expectEquals ((int) back.notes.size(), (int) voiced.notes.size());
            expect (li.mpe);
            // compare the glide of every note at its start
            auto sorted = voiced;
            sorted.sortByStart();
            for (size_t i = 0; i < std::min (back.notes.size(), sorted.notes.size()); ++i)
                expectWithinAbsoluteError (evalCurve (back.notes[i].bend, 0.0, 0.0f),
                                           evalCurve (sorted.notes[i].bend, 0.0, 0.0f), 0.02f);
        }
    }

    static Phrase chordProgressionForImport() { return chordProgression(); }
};

class CinematicTests : public juce::UnitTest
{
public:
    CinematicTests() : juce::UnitTest ("DarkMPE cinematic") {}

    void runTest() override
    {
        beginTest ("Root detection handles inversions");
        expectEquals (estimateRoot ({ 52, 55, 60 }), 0); // C/E
        expectEquals (estimateRoot ({ 52, 57, 60 }), 9); // Am/E
        expectEquals (estimateRoot ({ 45, 48, 52 }), 9);

        beginTest ("Reharm splits regions");
        const auto prog = chordProgression();
        expectEquals ((int) buildRegions (prog, Reharm::off, 16.0).size(), 4);
        expectEquals ((int) buildRegions (prog, Reharm::susResolve, 16.0).size(), 8);
        expectEquals ((int) buildRegions (prog, Reharm::chromaticApproach, 16.0).size(), 7);
        expectEquals ((int) buildRegions (prog, Reharm::mediantShift, 16.0).size(), 8);

        for (int m = 0; m < (int) Motion::count; ++m)
            for (int r = 0; r < (int) Reharm::count; ++r)
            {
                CineParams cp;
                cp.motion = (Motion) m;
                cp.reharm = (Reharm) r;
                cp.shape = (GlideShape) (r % (int) GlideShape::count);
                auto out = cinematic (prog, cp);
                const juce::String what = juce::String (motionNames[m]) + " / " + reharmNames[r];
                expect (! out.empty(), what);

                bool bends = false;
                for (const auto& n : out.notes)
                {
                    expect (n.lockedExpr);
                    expect (n.start >= 0.0 && n.end() <= prog.lengthBeats + 1.0e-6, what + " note outside phrase");
                    for (const auto& pt : n.bend)
                    {
                        expect (std::abs (pt.v) <= 48.0f, what + " bend out of range");
                        bends = bends || std::abs (pt.v) > 0.5f;
                    }
                    for (size_t i = 1; i < n.bend.size(); ++i)
                        expect (n.bend[i].t >= n.bend[i - 1].t, what + " curve not time-ordered");
                }
                expect (bends, what + " should move voices with pitch bend");

                shapeExpression (out, {});
                juce::String why;
                expect (channelsAreExclusive (renderMpe (out), why), what + ": " + why);
            }

        beginTest ("Morph: voices are long notes that bend into the next chord");
        {
            CineParams cp;
            cp.motion = Motion::morph;
            cp.reharm = Reharm::off;
            const auto out = cinematic (prog, cp);
            int longNotes = 0;
            for (const auto& n : out.notes)
                longNotes += n.length > 7.9 ? 1 : 0;
            expectGreaterThan (longNotes, 3);
            // A voice's bend at the start of bar 2 lands on a pitch of the F chord.
            for (const auto& n : out.notes)
                if (n.start == 0.0 && n.length > 8.0)
                {
                    const float atBar2 = evalCurve (n.bend, 6.2, 0.0f);
                    const int sounding = n.pitch + (int) std::lround (atBar2);
                    const int pc = ((sounding % 12) + 12) % 12;
                    expect (pc == 5 || pc == 9 || pc == 0 || pc == 7 || pc == 4,
                            "voice should be on an F-chord tone, got pc " + juce::String (pc));
                }
        }

        beginTest ("Bloom starts every chord from one unison point");
        {
            CineParams cp;
            cp.motion = Motion::bloom;
            cp.reharm = Reharm::off;
            cp.stagger = 0.0f;
            const auto out = cinematic (prog, cp);
            expectEquals ((int) out.notes.size(), 4 * cp.voices);
            std::set<int> startPitches;
            for (const auto& n : out.notes)
                if (n.start == 0.0)
                    startPitches.insert (n.pitch + (int) std::lround (n.bend.front().v));
            expectEquals ((int) startPitches.size(), 1);
        }

        beginTest ("Demo progression");
        expectEquals ((int) demoProgression (9, scales::Scale::naturalMinor, 4).notes.size(), 12);
    }
};

// Most notes sounding at the same time.
int maxPolyphony (const Phrase& p)
{
    std::vector<std::pair<double, int>> edges;
    for (const auto& n : p.notes)
    {
        edges.push_back ({ n.start, 1 });
        edges.push_back ({ n.end(), -1 });
    }
    std::sort (edges.begin(), edges.end()); // ends (-1) sort before starts at the same time
    int cur = 0, most = 0;
    for (auto [t, d] : edges)
        most = std::max (most, cur += d);
    return most;
}

// Sounding pitch (note + bend) of the lowest voice at an absolute time, or -1000.
float lowestAt (const Phrase& p, double t)
{
    float lo = 1000.0f;
    for (const auto& n : p.notes)
        if (n.start <= t && n.end() > t)
            lo = std::min (lo, (float) n.pitch + evalCurve (n.bend, t - n.start, 0.0f));
    return lo;
}

class HarmonyTests : public juce::UnitTest
{
public:
    HarmonyTests() : juce::UnitTest ("DarkMPE cinematic 2.0") {}

    void runTest() override
    {
        beginTest ("Progressions: roots, length, chord symbols");
        {
            HarmonyParams hp;
            hp.key = 9;
            hp.progression = Progression::epicMinor;
            hp.bars = 4;
            const auto regions = generateProgression (hp);
            expectEquals ((int) regions.size(), 4);
            const int roots[] = { 9, 5, 0, 7 }; // A F C G
            for (size_t i = 0; i < regions.size(); ++i)
                expectEquals (estimateRoot (regions[i].pitches), roots[i]);
            expectEquals (juce::String (chordSymbol (regions[0])), juce::String ("Am"));
            expectEquals (juce::String (chordSymbol (regions[1])), juce::String ("F"));

            Region r;
            r.pitches = { 57, 60, 64, 68 };
            expectEquals (juce::String (chordSymbol (r)), juce::String ("Am(maj7)"));
            r.pitches = { 58, 62, 65 };
            r.bassPc = 9;
            expectEquals (juce::String (chordSymbol (r)), juce::String ("A#/A"));

            hp.progression = Progression::lamentBass;
            const auto lament = generateProgression (hp);
            const int basses[] = { -1, 7, 5, -1 }; // A  G  F  E  (key A: v over b7 = G, iv over b6 = F, V in root position)
            for (int i = 1; i < 4; ++i)
                expectEquals (lament[(size_t) i].bassPc, basses[i]);

            for (int prog = 0; prog < (int) Progression::count; ++prog)
                for (int len = 0; len < (int) ChordLength::count; ++len)
                {
                    hp.progression = (Progression) prog;
                    hp.chordLength = (ChordLength) len;
                    hp.bars = 8;
                    const auto rs = generateProgression (hp);
                    double end = 0.0;
                    for (const auto& reg : rs)
                    {
                        expect (reg.pitches.size() >= 3, progressionNames[prog]);
                        expect (std::abs (reg.start - end) < 1.0e-9, "regions must be contiguous");
                        end = reg.start + reg.length;
                    }
                    expect (std::abs (end - 32.0) < 1.0e-9, juce::String (progressionNames[prog]) + ": must fill the bars");
                }

            // Auto is seeded: same seed, same chords; the last chord leads home.
            hp.progression = Progression::autoSeed;
            hp.chordLength = ChordLength::oneBar;
            const auto a = generateProgression (hp), b = generateProgression (hp);
            bool same = a.size() == b.size();
            for (size_t i = 0; same && i < a.size(); ++i)
                same = a[i].pitches == b[i].pitches;
            expect (same, "Auto progression must be deterministic");
        }

        beginTest ("Tension adds colours, darkness picks dark ones");
        {
            HarmonyParams hp;
            auto plain = generateProgression (hp);
            auto none = plain;
            colourRegions (none, 0.0f, 0.5f, 1);
            for (size_t i = 0; i < none.size(); ++i)
                expect (none[i].pitches == plain[i].pitches, "tension 0 keeps the chords");

            auto full = plain;
            colourRegions (full, 1.0f, 1.0f, 1);
            for (size_t i = 0; i < full.size(); ++i)
                expectGreaterThan (full[i].pitches.size(), plain[i].pitches.size(), "tension 1 colours every chord");
        }

        beginTest ("Tonic Pedal keeps the tonic in the bass, Planing keeps the shape");
        {
            HarmonyParams hp;
            hp.progression = Progression::epicMinor;
            CineParams cp;
            cp.reharm = Reharm::tonicPedal;
            cp.motion = Motion::bloom;
            cp.stagger = 0.0f;
            cp.glide = 0.2f;
            const auto out = cinematicRegions (generateProgression (hp), 16.0, cp, hp.key);
            for (double t : { 3.0, 7.0, 11.0, 15.0 })
                expectEquals (scales::mod ((int) std::lround (lowestAt (out, t)), 12), 9, "bass must stay on A");

            const auto planed = reharmonise (generateProgression (hp), Reharm::planing, 9);
            for (const auto& reg : planed)
            {
                std::vector<int> shape, first;
                for (int x : reg.pitches) shape.push_back (x - reg.pitches.front());
                for (int x : planed.front().pitches) first.push_back (x - planed.front().pitches.front());
                expect (shape == first, "planing must keep the interval structure");
            }
        }

        beginTest ("Deep Note grows out of a cluster, Pulse strikes on the grid");
        {
            HarmonyParams hp;
            CineParams cp;
            cp.motion = Motion::deepNote;
            cp.reharm = Reharm::off;
            const auto regions = generateProgression (hp);
            const auto out = cinematicRegions (regions, 16.0, cp, hp.key);
            float lo = 1000.0f, hi = -1000.0f;
            for (const auto& n : out.notes)
                if (n.start == 0.0)
                {
                    const float v = (float) n.pitch + n.bend.front().v;
                    lo = std::min (lo, v);
                    hi = std::max (hi, v);
                    // settled on a chord tone of Am by the end of the first bar
                    const int pc = scales::mod ((int) std::lround ((float) n.pitch + evalCurve (n.bend, 2.6, 0.0f)), 12);
                    expect (pc == 9 || pc == 0 || pc == 4, "deep note must land on the chord, got pc " + juce::String (pc));
                }
            expectLessOrEqual (hi - lo, 14.0f, "deep note must start as a cluster");

            cp.motion = Motion::pulse;
            cp.pulse = 0.5f; // 10 hits per bar
            cp.voices = 6;
            const auto pulse = cinematicRegions (regions, 16.0, cp, hp.key);
            expectEquals ((int) pulse.notes.size(), 10 * 4 * 6);
        }

        beginTest ("Stepped glides hold on scale notes");
        {
            HarmonyParams hp;
            CineParams cp;
            cp.motion = Motion::bloom;
            cp.reharm = Reharm::off;
            cp.shape = GlideShape::stepped;
            cp.swell = 0.0f;
            cp.key = 9;
            cp.scale = scales::Scale::naturalMinor;
            const auto out = cinematicRegions (generateProgression (hp), 16.0, cp, hp.key);
            int holds = 0;
            for (const auto& n : out.notes)
            {
                if (n.voiceIndex >= cp.voices - 1)
                    continue; // the top voice carries vibrato
                for (size_t i = 1; i < n.bend.size(); ++i)
                    if (std::abs (n.bend[i].v - n.bend[i - 1].v) < 1.0e-4f && n.bend[i].t - n.bend[i - 1].t > 0.02)
                    {
                        const int pitch = n.pitch + (int) std::lround (n.bend[i].v);
                        expect (scales::inScale (pitch, 9, scales::Scale::naturalMinor), "stepped glide held off-scale");
                        ++holds;
                    }
            }
            expectGreaterThan (holds, 10);
        }

        beginTest ("Every progression x motion x reharm renders valid MPE");
        {
            int combos = 0;
            for (int prog = 0; prog < (int) Progression::count; ++prog)
                for (int m = 0; m < (int) Motion::count; ++m)
                    for (int r = 0; r < (int) Reharm::count; ++r)
                    {
                        HarmonyParams hp;
                        hp.progression = (Progression) prog;
                        hp.bars = 4;
                        hp.chordLength = (ChordLength) ((prog + r) % (int) ChordLength::count);
                        CineParams cp;
                        cp.motion = (Motion) m;
                        cp.reharm = (Reharm) r;
                        cp.shape = (GlideShape) ((m + r) % (int) GlideShape::count);
                        cp.voicing = (r % 3 == 0) ? VoicingMode::gothic : (r % 3 == 1 ? VoicingMode::hyperSpread : VoicingMode::epicSpread);
                        cp.voices = 4 + (prog + m) % 5; // 4..8
                        cp.sub = (prog + m + r) % 2 == 0;
                        cp.tension = (float) ((prog * 7 + r) % 5) / 4.0f;
                        cp.darkness = (float) (m % 3) / 2.0f;
                        cp.fall = r % 2 == 0 ? 0.6f : 0.0f;
                        cp.arc = 0.5f;
                        const juce::String what = juce::String (progressionNames[prog]) + " / " + motionNames[m] + " / " + reharmNames[r];

                        auto out = cinematicRegions (generateProgression (hp), 16.0, cp, hp.key);
                        expect (! out.empty(), what);
                        for (const auto& n : out.notes)
                        {
                            expect (n.start >= -1.0e-9 && n.end() <= 16.0 + 1.0e-6, what + " note outside phrase");
                            for (size_t i = 0; i < n.bend.size(); ++i)
                            {
                                expect (std::abs (n.bend[i].v) <= 48.0f, what + " bend out of range");
                                if (i > 0)
                                    expect (n.bend[i].t >= n.bend[i - 1].t, what + " curve not time-ordered");
                            }
                        }
                        expectLessOrEqual (maxPolyphony (out), 15, what + " exceeds the MPE zone");

                        shapeExpression (out, {});
                        juce::String why;
                        expect (channelsAreExclusive (renderMpe (out), why), what + ": " + why);
                        ++combos;
                    }
            std::cout << "    " << combos << " combinations checked" << std::endl;
        }
    }
};

static HarmonyTests harmonyTests;

class KitTests : public juce::UnitTest
{
public:
    KitTests() : juce::UnitTest ("DarkMPE kit") {}

    void runTest() override
    {
        beginTest ("Every layer and pattern: in the loop, deterministic, valid MPE");
        {
            const int patternCounts[] = { 1, (int) BassPattern::count, (int) ArpPattern::count, (int) SirenPattern::count, (int) StabPattern::count, 1 };
            for (int style = 0; style < (int) Style::count; ++style)
                for (int pat = 0; pat < 4; ++pat)
                    for (float density : { 0.0f, 1.0f })
                    {
                        KitParams kp;
                        kp.gen.style = (Style) style;
                        kp.gen.bars = 4;
                        kp.gen.seed = 11 + style;
                        for (int l = 0; l < numLayers; ++l)
                        {
                            kp.layers[(size_t) l].pattern = pat % patternCounts[l];
                            kp.layers[(size_t) l].density = density;
                        }
                        const auto a = generateKit (kp), b = generateKit (kp);
                        expectEquals ((int) a.size(), numLayers);
                        for (size_t i = 0; i < a.size(); ++i)
                        {
                            const juce::String what = juce::String (styleNames[style]) + " / " + layerNames[(int) a[i].layer]
                                                    + " pattern " + juce::String (pat) + " density " + juce::String (density);
                            const auto& ph = a[i].phrase;
                            expect (! ph.empty(), what + " is empty");
                            expectEquals (ph.lengthBeats, 16.0);
                            expectEquals ((int) ph.notes.size(), (int) b[i].phrase.notes.size(), what + " not deterministic");
                            for (const auto& n : ph.notes)
                                expect (n.start >= 0.0 && n.end() <= ph.lengthBeats + 1.0e-6, what + " note outside the loop");
                            expectLessOrEqual (maxPolyphony (ph), 15, what);

                            auto shaped = ph;
                            shapeExpression (shaped, {});
                            juce::String why;
                            expect (channelsAreExclusive (renderMpe (shaped), why), what + ": " + why);
                        }
                    }
        }

        beginTest ("Registers, scale, stabs on the lead's accents, moving siren");
        {
            KitParams kp;
            kp.gen.bars = 8;
            kp.layers[(size_t) Layer::stab].pattern = (int) StabPattern::accents;
            const auto parts = generateKit (kp);
            auto part = [&] (Layer l) -> const Phrase& { for (const auto& p : parts) if (p.layer == l) return p.phrase; return parts.front().phrase; };

            for (const auto& n : part (Layer::bass).notes)
            {
                expectLessThan (n.pitch, 60, "bass too high");
                expect (scales::inScale (n.pitch, kp.gen.key, kp.gen.scale), "bass out of scale");
            }
            for (const auto& n : part (Layer::arp).notes)
            {
                expect (n.pitch >= 48 && n.pitch <= 100, "arp register");
                expect (scales::inScale (n.pitch, kp.gen.key, kp.gen.scale), "arp out of scale");
            }

            std::set<double> accents;
            for (const auto& n : part (Layer::lead).notes)
                if (n.accent)
                    accents.insert (std::round (n.start * 4.0) / 4.0);
            for (const auto& n : part (Layer::stab).notes)
                expect (accents.count (n.start) != 0, "stab not on a lead accent: " + juce::String (n.start));

            float sirenMove = 0.0f;
            for (const auto& n : part (Layer::siren).notes)
                for (const auto& pt : n.bend)
                    sirenMove = std::max (sirenMove, std::abs (pt.v));
            expectGreaterThan (sirenMove, 2.0f, "the siren must bend");

            for (const auto& n : part (Layer::pad).notes)
                expect (n.lockedExpr);

            kp.layers[(size_t) Layer::arp].on = false;
            kp.layers[(size_t) Layer::siren].on = false;
            expectEquals ((int) generateKit (kp).size(), numLayers - 2);
        }
    }
};

static KitTests kitTests;

class FormTests : public juce::UnitTest
{
public:
    FormTests() : juce::UnitTest ("DarkMPE phrase forms") {}

    // Notes of one bar as (position in the bar, pitch).
    static std::vector<std::pair<long, int>> bar (const Phrase& p, int b)
    {
        std::vector<std::pair<long, int>> v;
        for (const auto& n : p.notes)
            if (n.start >= b * 4.0 - 1.0e-9 && n.start < b * 4.0 + 4.0 - 1.0e-9)
                v.push_back ({ std::lround ((n.start - b * 4.0) * 1000.0), n.pitch });
        return v;
    }
    static std::vector<long> rhythm (const Phrase& p, int b)
    {
        std::vector<long> v;
        for (auto [t, pitch] : bar (p, b))
            v.push_back (t);
        return v;
    }

    void runTest() override
    {
        beginTest ("Sections of each form");
        {
            auto labels = [] (Form f, int n) { juce::String s; for (const auto& sec : formSections (f, n)) s << sec.label << " "; return s.trim(); };
            expectEquals (labels (Form::abac, 4), juce::String ("A B A C"));
            expectEquals (labels (Form::abac, 8), juce::String ("A B A C A B A C"));
            expectEquals (labels (Form::period, 4), juce::String ("A B A B'"));
            expectEquals (labels (Form::sentence, 4), juce::String ("A A' F C"));
            expectEquals (labels (Form::sequence, 4), juce::String ("A A+ A++ C"));
            expectEquals (labels (Form::callResponse, 2), juce::String ("A B"));
            expect (formSections (Form::classic, 4).empty());
        }

        beginTest ("A B A C: A comes back note for note, B answers, C closes on the root");
        {
            GenParams gp;
            gp.style = Style::opr; // i - bII - i - bII: bars 1 and 3 share the chord
            gp.form = Form::abac;
            gp.bars = 4;
            gp.chroma = 0.4f;
            gp.seed = 99;
            const auto mel = generateMelody (gp);
            expect (bar (mel, 0) == bar (mel, 2), "A must repeat exactly (chromatic notes included)");
            expect (bar (mel, 1) != bar (mel, 3), "B and C must differ");
            expect (rhythm (mel, 0).front() == rhythm (mel, 1).front(), "the answer starts like the call");

            const auto& prog = styleProgression (gp.style);
            const int chordRoot = scales::mod (scales::degreeToPitch (gp.key, gp.scale, prog[3]), 12);
            const auto& last = mel.notes.back();
            expectEquals (scales::mod (last.pitch, 12), chordRoot, "C must end on the chord root");
            expect (last.end() > 15.9 - 1.0e-6 || mel.lengthBeats - last.end() < 0.05, "the closing note is held");
            expect (isMonophonic (mel));

            // MUTATE re-draws the answers, never A.
            auto mutated = gp;
            mutated.variation = 1;
            const auto m2 = generateMelody (mutated);
            expect (bar (m2, 0) == bar (mel, 0), "MUTATE must keep A");
        }

        beginTest ("A A A B and Sequence keep the rhythm of A");
        {
            GenParams gp;
            gp.form = Form::aaab;
            gp.chroma = 0.0f;
            const auto aaab = generateMelody (gp);
            expect (rhythm (aaab, 0) == rhythm (aaab, 1) && rhythm (aaab, 1) == rhythm (aaab, 2), "A A A: same rhythm");

            gp.form = Form::sequence;
            gp.style = Style::opr;
            const auto seq = generateMelody (gp);
            expect (rhythm (seq, 0) == rhythm (seq, 2), "A++ keeps the rhythm of A");
            expect (bar (seq, 0) != bar (seq, 2), "A++ moves the idea up");
        }

        beginTest ("Every form x style: in scale, one line, deterministic");
        for (int f = 0; f < (int) Form::count; ++f)
            for (int st = 0; st < (int) Style::count; ++st)
            {
                GenParams gp;
                gp.form = (Form) f;
                gp.style = (Style) st;
                gp.bars = 8;
                gp.seed = 5 + f * 17 + st;
                const auto a = generateMelody (gp), b = generateMelody (gp);
                const juce::String what = juce::String (formNames[f]) + " / " + styleNames[st];
                expect (! a.empty(), what);
                expect (bar (a, 3) == bar (b, 3), what + " not deterministic");
                for (const auto& n : a.notes)
                {
                    expect (n.chromatic || scales::inScale (n.pitch, gp.key, gp.scale), what + " out of scale");
                    expect (n.start >= 0.0 && n.end() <= a.lengthBeats + 1.0e-6, what + " note outside the loop");
                }
                expect (isMonophonic (a), what + " must stay one line");
            }

        beginTest ("Forms shape cinematic progressions");
        {
            HarmonyParams hp;
            hp.progression = Progression::epicMinor;
            hp.form = Form::abac;
            SectionMarks marks;
            const auto abac = generateProgression (hp, &marks);
            juce::String chords;
            for (const auto& r : abac) chords << chordSymbol (r) << " ";
            expectEquals (chords.trim(), juce::String ("Am F Am E7"));
            expectEquals ((int) marks.size(), 4);

            hp.form = Form::sequence;
            hp.bars = 8;
            chords = {};
            for (const auto& r : generateProgression (hp)) chords << chordSymbol (r) << " ";
            expectEquals (chords.trim(), juce::String ("Am F Bm G Cm G# Dm E7"));

            for (int f = 0; f < (int) Form::count; ++f)
                for (int prog = 0; prog < (int) Progression::count; ++prog)
                {
                    hp.form = (Form) f;
                    hp.progression = (Progression) prog;
                    hp.bars = 8;
                    auto out = cinematicRegions (generateProgression (hp), 32.0, {}, hp.key);
                    shapeExpression (out, {});
                    juce::String why;
                    expect (channelsAreExclusive (renderMpe (out), why), juce::String (formNames[f]) + " / " + progressionNames[prog] + ": " + why);
                }
        }

        beginTest ("KIT layers follow the form");
        {
            KitParams kp;
            kp.gen.form = Form::abac;
            kp.gen.style = Style::opr;
            const auto parts = generateKit (kp);
            for (const auto& part : parts)
                if (part.layer == Layer::arp || part.layer == Layer::bass || part.layer == Layer::lead)
                    expect (bar (part.phrase, 0) == bar (part.phrase, 2), juce::String (layerNames[(int) part.layer]) + ": A bars must repeat");
        }
    }
};

static FormTests formTests;

// Pitch-bend steps of the note `pitch` between its note-on and `window` beats later (semitones, range 48).
struct GlideStats { int bends = 0; float maxStep = 0.0f; float last = 0.0f; };
GlideStats glideStats (const juce::MidiMessageSequence& seq, int pitch, double window)
{
    GlideStats g;
    int ch = -1;
    double on = 0.0;
    float prev = 0.0f;
    bool havePrev = false;
    for (auto* ev : seq)
    {
        const auto& m = ev->message;
        if (ch < 0 && m.isNoteOn() && m.getNoteNumber() == pitch)
        {
            ch = m.getChannel();
            on = m.getTimeStamp();
        }
    }
    for (auto* ev : seq)
    {
        const auto& m = ev->message;
        if (m.getChannel() != ch || ! m.isPitchWheel() || m.getTimeStamp() < on - 1.0e-9 || m.getTimeStamp() > on + window)
            continue;
        const float v = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 48.0f;
        if (havePrev)
        {
            g.maxStep = std::max (g.maxStep, std::abs (v - prev));
            ++g.bends;
        }
        prev = v;
        havePrev = true;
        g.last = v;
    }
    return g;
}

int countEvents (const juce::MidiMessageSequence& seq)
{
    int pb = 0, cc = 0, at = 0, notes = 0;
    for (auto* ev : seq)
    {
        const auto& m = ev->message;
        pb += m.isPitchWheel() ? 1 : 0;
        cc += m.isController() ? 1 : 0;
        at += m.isChannelPressure() ? 1 : 0;
        notes += m.isNoteOn() ? 1 : 0;
    }
    std::cout << "      [notes " << notes << " bend " << pb << " cc74 " << cc << " pressure " << at << "]" << std::endl;
    return seq.getNumEvents();
}

class RenderTests : public juce::UnitTest
{
public:
    RenderTests() : juce::UnitTest ("Render smoothness") {}

    void runTest() override
    {
        beginTest ("An octave glide is rendered in small steps");
        {
            Phrase p;
            p.lengthBeats = 4.0;
            Note a;
            a.start = 0.0;
            a.length = 1.0;
            a.pitch = 60;
            Note b = a;
            b.start = 1.0;
            b.pitch = 72;
            b.glideFrom = 60;
            p.notes = { a, b };

            ExprParams ep;
            ep.vibratoDepth = 0.0f;
            ep.detuneCents = 0.0f;
            shapeExpression (p, ep);
            RenderOptions ro;
            ro.includeZoneConfig = false;
            const auto g = glideStats (renderMpe (p, ro), 72, ep.glideTime + 0.02);
            std::cout << "    octave glide: " << g.bends << " bend steps, largest " << g.maxStep << " st" << std::endl;
            expectGreaterThan (g.bends, 16, "glide rendered with too few pitch-bend steps");
            expectLessOrEqual (g.maxStep, 1.2f, "glide has a large pitch jump");
            expectLessOrEqual (std::abs (g.last), 0.03f, "glide must land on the note");
        }

        beginTest ("Mono render: one line on channel 1, legato glides, bends in range");
        {
            GenParams gp;
            gp.slide = 1.0f;
            gp.gate = 1.0f;
            gp.bars = 8;
            Phrase lead = generateMelody (gp);
            shapeExpression (lead, {});
            const int range = 12;
            const auto seq = renderMono (lead, range, true);

            int sounding = 0, notes = 0, legato = 0, bends = 0;
            bool rpn = false;
            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                const auto& m = seq.getEventPointer (i)->message;
                expectEquals (m.getChannel(), 1, "mono output must stay on channel 1");
                if (m.isController() && m.getControllerNumber() == 6 && m.getControllerValue() == range)
                    rpn = true;
                if (m.isNoteOn())
                {
                    ++notes;
                    // A second note may only overlap for the legato instant (its note-on first, same time).
                    if (sounding == 1)
                    {
                        ++legato;
                        const auto& next = seq.getEventPointer (i + 1)->message;
                        expect (next.isNoteOff() && next.getTimeStamp() - m.getTimeStamp() < 1.0e-5, "notes overlap");
                    }
                    ++sounding;
                    expectLessOrEqual (sounding, 2);
                }
                else if (m.isNoteOff())
                    --sounding;
                else if (m.isPitchWheel() && m.getPitchWheelValue() != 8192)
                    ++bends;
            }
            expect (rpn, "pitch-bend range must be set");
            expectEquals (sounding, 0, "hanging notes");
            expectGreaterThan (notes, 20);
            expectGreaterThan (legato, 3, "glides should tie notes legato");
            expectGreaterThan (bends, 20, "glides should bend");
        }

        beginTest ("Event density stays reasonable");
        {
            Phrase cine = cinematic (demoProgression (9, scales::Scale::naturalMinor, 8), {});
            shapeExpression (cine, {});
            GenParams gp;
            gp.bars = 8;
            Phrase lead = generateMelody (gp);
            shapeExpression (lead, {});
            const int c = countEvents (renderMpe (cine));
            const int l = countEvents (renderMpe (lead));
            std::cout << "    events: cinematic demo 8 bars " << c << ", lead 8 bars " << l << std::endl;
            expectLessThan (c, cineBudget);
            expectLessThan (l, leadBudget);
        }
    }

    // Regression guards (measured: ~11.8k and ~3k; the 1/32-beat renderer gave 10.3k and 1.7k with 4-step glides).
    static constexpr int cineBudget = 14000, leadBudget = 4000;
};

static RenderTests renderTests;

// Notes before `upTo` beats as (start in ms of beats, pitch, glides in).
static std::vector<std::tuple<long, int, bool>> notesBefore (const Phrase& p, double upTo)
{
    std::vector<std::tuple<long, int, bool>> v;
    for (const auto& n : p.notes)
        if (n.start < upTo - 1.0e-9)
            v.push_back ({ std::lround (n.start * 1000.0), n.pitch, n.glideFrom >= 0 });
    return v;
}

class V2Tests : public juce::UnitTest
{
public:
    V2Tests() : juce::UnitTest ("DarkMPE V2: harmony, stability, scale lock, rate, voice") {}

    void runTest() override
    {
        const ExprParams expr;

        beginTest ("More bars keep the first bars (both engines, every harmony, with and without a form)");
        for (int engine = 0; engine < (int) LeadEngine::count; ++engine)
            for (int harmony = 0; harmony < (int) HarmonySource::count; ++harmony)
                for (Form form : { Form::classic, Form::abac, Form::sentence })
                    for (int seed = 1; seed <= 12; ++seed)
                    {
                        GenParams gp;
                        gp.engine = (LeadEngine) engine;
                        gp.harmony = (HarmonySource) harmony;
                        gp.progression = (Progression) (seed % (int) Progression::count);
                        gp.form = form;
                        gp.seed = seed;
                        gp.style = (Style) (seed % (int) Style::count);
                        gp.voice = (VoiceStyle) (seed % (int) VoiceStyle::count);
                        gp.bars = 4;
                        const auto four = generateLead (gp, expr);
                        gp.bars = 8;
                        const auto eight = generateLead (gp, expr);
                        gp.bars = 2;
                        const auto two = generateLead (gp, expr);
                        const juce::String what = juce::String (leadEngineNames[engine]) + " / " + harmonySourceNames[harmony] + " / "
                                                + formNames[(int) form] + " / seed " + juce::String (seed);
                        expect (notesBefore (four, 12.0) == notesBefore (eight, 12.0), what + ": 4 -> 8 bars changed the first bars");
                        expect (notesBefore (two, 4.0) == notesBefore (four, 4.0), what + ": 2 -> 4 bars changed the first bar");
                    }

        beginTest ("More density only adds notes");
        for (int style = 0; style < (int) Style::count; ++style)
        {
            if ((Style) style == Style::darkArp)
                continue; // an arpeggio walks through the chord note by note: more notes re-sequence it
            for (int seed = 1; seed <= 15; ++seed)
                for (float d = 0.0f; d < 0.9f; d += 0.15f)
                {
                    GenParams gp;
                    gp.style = (Style) style;
                    gp.seed = seed;
                    gp.chroma = 0.0f; // approach notes and slides depend on the next note
                    gp.slide = 0.0f;
                    gp.density = d;
                    const auto sparse = generateMelody (gp);
                    gp.density = d + 0.15f;
                    const auto dense = generateMelody (gp);
                    for (const auto& n : sparse.notes)
                    {
                        const bool kept = std::any_of (dense.notes.begin(), dense.notes.end(), [&n] (const Note& m)
                                                       { return std::abs (m.start - n.start) < 1.0e-9 && m.pitch == n.pitch; });
                        expect (kept, juce::String (styleNames[style]) + " density " + juce::String (d) + ": a note moved");
                        if (! kept)
                            break;
                    }
                }
        }

        beginTest ("Scale Lock: every note of every layer in the scale");
        for (int scale = 0; scale < (int) scales::Scale::count; ++scale)
            for (int harmony = 0; harmony < (int) HarmonySource::count; ++harmony)
                for (int seed = 1; seed <= 6; ++seed)
                {
                    KitParams kp;
                    kp.gen.scale = (scales::Scale) scale;
                    kp.gen.harmony = (HarmonySource) harmony;
                    kp.gen.progression = (Progression) ((seed * 3 + scale) % (int) Progression::count);
                    kp.gen.chordLength = (ChordLength) (seed % 3);
                    kp.gen.seed = seed;
                    kp.gen.style = (Style) (seed % (int) Style::count);
                    kp.gen.chroma = 0.5f;
                    kp.gen.engine = (LeadEngine) (seed % 2);
                    kp.gen.scaleLock = true;
                    kp.gen.form = seed % 3 == 0 ? Form::abac : Form::classic;
                    for (auto& l : kp.layers)
                        l.pattern = seed % 4;
                    kp.layers[(size_t) Layer::siren].pattern = seed % 2 == 0 ? (int) SirenPattern::alarm : (int) SirenPattern::fall;
                    kp.pad.tension = 0.8f;
                    kp.pad.darkness = 1.0f;
                    kp.pad.reharm = (Reharm) (seed % (int) Reharm::count);
                    kp.pad.voicing = seed % 2 == 0 ? VoicingMode::gothic : VoicingMode::darkCluster;

                    for (const auto& part : generateKit (kp))
                    {
                        const juce::String what = juce::String (scales::scaleNames[scale]) + " / " + harmonySourceNames[harmony] + " / "
                                                + layerNames[(int) part.layer] + " / seed " + juce::String (seed);
                        for (const auto& n : part.phrase.notes)
                        {
                            expect (scales::inScale (n.pitch, kp.gen.key, kp.gen.scale), what + ": " + juce::String (n.pitch) + " out of the scale");
                            if (part.layer == Layer::siren && ! n.bend.empty())
                            {
                                // the bends land on scale notes: the highest point (alarm: the third) and the end (fall)
                                float top = 0.0f;
                                for (const auto& pt : n.bend)
                                    top = std::max (top, pt.v);
                                expect (scales::inScale (n.pitch + (int) std::lround (top), kp.gen.key, kp.gen.scale), what + ": siren peak");
                                if (kp.layers[(size_t) Layer::siren].pattern == (int) SirenPattern::fall)
                                    expect (scales::inScale (n.pitch + (int) std::lround (n.bend.back().v), kp.gen.key, kp.gen.scale), what + ": siren fall");
                            }
                        }
                    }
                }

        beginTest ("Without Scale Lock: chromatic approaches and borrowed chords come back");
        {
            GenParams gp;
            gp.scaleLock = false;
            gp.chroma = 0.6f;
            gp.bars = 8;
            int chromatic = 0;
            for (const auto& n : generateMelody (gp).notes)
                chromatic += n.chromatic ? 1 : 0;
            expectGreaterThan (chromatic, 0);

            // E7 in A natural minor: the lines use A harmonic minor over it (G#), the key scale elsewhere.
            gp.scale = scales::Scale::naturalMinor;
            gp.harmony = HarmonySource::progression;
            gp.progression = Progression::harmonicDominant; // Am F Dm E7
            const auto track = leadHarmony (gp);
            expectEquals ((int) track.spans.size(), 8);
            expectEquals ((int) track.spans[3].scale, (int) scales::Scale::harmonicMinor);
            expectEquals (track.spans[3].scaleTonic, 9);
            expect (track.contains (12.0, 68), "G# over E7");
            expect (! track.contains (0.0, 68), "no G# over Am");

            gp.scaleLock = true; // E7 -> Em7: no G# anywhere
            const auto locked = leadHarmony (gp);
            expectEquals (juce::String (chordSymbol (locked.spans[3].chord)), juce::String ("Em7"));
            expect (! locked.contains (12.0, 68));
        }

        beginTest ("The lead and the bass follow the chords");
        for (int harmony = 0; harmony < (int) HarmonySource::count; ++harmony)
            for (int prog = 0; prog < (int) Progression::count; ++prog)
                for (int len = 0; len < (int) ChordLength::count; ++len)
                {
                    KitParams kp;
                    kp.gen.harmony = (HarmonySource) harmony;
                    kp.gen.progression = (Progression) prog;
                    kp.gen.chordLength = (ChordLength) len;
                    kp.gen.scaleLock = prog % 2 == 0;
                    kp.gen.bars = 8;
                    kp.gen.seed = 3 + prog;
                    kp.layers[(size_t) Layer::bass].pattern = (int) BassPattern::rolling;
                    const auto track = leadHarmony (kp.gen);
                    const juce::String what = juce::String (harmonySourceNames[harmony]) + " / " + progressionNames[prog] + " / "
                                            + chordLengthNames[len];

                    for (const auto& part : generateKit (kp))
                    {
                        if (part.layer == Layer::lead)
                        {
                            // Every bar opens on the root of its chord (the pedal of the riff).
                            for (int bar = 0; bar < 8; ++bar)
                                for (const auto& n : part.phrase.notes)
                                    if (std::abs (n.start - bar * 4.0) < 1.0e-9)
                                        expectEquals (scales::mod (n.pitch, 12), scales::mod (track.at (bar * 4.0).chord.pitches.front(), 12),
                                                      what + ": bar " + juce::String (bar) + " does not start on the chord root");
                        }
                        if (part.layer == Layer::bass)
                            for (const auto& n : part.phrase.notes)
                            {
                                const auto& chord = track.at (std::floor (n.start)).chord;
                                const int want = chord.bassPc >= 0 ? chord.bassPc : scales::mod (chord.pitches.front(), 12);
                                expectEquals (scales::mod (n.pitch, 12), want, what + ": the bass is not on the chord's bass");
                            }
                    }
                }
        {
            // Tonic: no changes at all.
            GenParams gp;
            gp.harmony = HarmonySource::tonic;
            gp.bars = 8;
            const auto track = leadHarmony (gp);
            expectEquals ((int) track.spans.size(), 1);
            for (int bar = 0; bar < 8; ++bar)
                expectEquals (track.pitch (bar * 4.0, 57, 0), 57);
        }

        beginTest ("Rate: the grid of the lead");
        for (int rate = 0; rate < (int) Rate::count; ++rate)
            for (int style = 0; style < (int) Style::count; ++style)
            {
                GenParams gp;
                gp.rate = (Rate) rate;
                gp.style = (Style) style;
                gp.seed = 20 + style;
                const auto mel = generateMelody (gp);
                const double step = 4.0 / stepsPerBar (gp.rate);
                expect (! mel.empty());
                for (const auto& n : mel.notes)
                {
                    const double k = n.start / step;
                    expectWithinAbsoluteError (k, std::round (k), 1.0e-6, juce::String (rateNames[rate]) + " / " + styleNames[style] + ": off the grid");
                }
                expect (isMonophonic (mel));
            }
        {
            // 1/8: the 8ths are the 16th grid's 8ths, so the line keeps its notes there.
            GenParams gp;
            gp.density = 1.0f;
            gp.style = Style::hateOrGlory;
            gp.rate = Rate::eighth;
            const auto eighths = generateMelody (gp);
            int onBeat = 0;
            for (const auto& n : eighths.notes)
                onBeat += std::abs (std::fmod (n.start, 0.5)) < 1.0e-9 ? 1 : 0;
            expectEquals (onBeat, (int) eighths.notes.size());
        }

        beginTest ("Long Notes: quarters and halves among the 16ths");
        {
            GenParams gp;
            gp.density = 1.0f;
            gp.gate = 0.6f;
            gp.bars = 8;
            auto longest = [] (const Phrase& p) { double m = 0.0; for (const auto& n : p.notes) m = std::max (m, n.length); return m; };
            gp.longNotes = 0.0f;
            const auto plain = generateMelody (gp);
            gp.longNotes = 1.0f;
            const auto held = generateMelody (gp);
            expectLessThan (longest (plain), 0.3);
            expectGreaterThan (longest (held), 0.45);
            expectLessThan (held.notes.size(), plain.notes.size());
        }

        beginTest ("Voice: syllables that talk (vowels, loudness, inflection), one line, in the scale");
        for (int voice = 0; voice < (int) VoiceStyle::count; ++voice)
            for (int form = 0; form < (int) Form::count; form += 3)
                for (int seed = 1; seed <= 4; ++seed)
                {
                    GenParams gp;
                    gp.engine = LeadEngine::voice;
                    gp.voice = (VoiceStyle) voice;
                    gp.form = (Form) form;
                    gp.seed = seed;
                    gp.bars = 4;
                    gp.rate = seed == 4 ? Rate::sixteenthTriplet : Rate::sixteenth;
                    const auto a = generateLead (gp, expr), b = generateLead (gp, expr);
                    const juce::String what = juce::String (voiceStyleNames[voice]) + " / " + formNames[form] + " / seed " + juce::String (seed);

                    expect (! a.empty(), what);
                    expect (notesBefore (a, 16.0) == notesBefore (b, 16.0), what + ": not deterministic");
                    expectLessOrEqual (maxPolyphony (a), 1, what + ": one line");
                    float slideLow = 1.0f, slideHigh = 0.0f;
                    for (const auto& n : a.notes)
                    {
                        expect (scales::inScale (n.pitch, gp.key, gp.scale), what + ": out of the scale");
                        expect (n.lockedExpr && ! n.bend.empty() && ! n.slide.empty() && ! n.pressure.empty(), what + ": no expression");
                        expect (n.start >= 0.0 && n.end() <= a.lengthBeats + 1.0e-6, what + ": outside the loop");
                        for (const auto& pt : n.slide)
                        {
                            slideLow = std::min (slideLow, pt.v);
                            slideHigh = std::max (slideHigh, pt.v);
                        }
                        for (const auto& pt : n.pressure)
                            expect (pt.v >= 0.0f && pt.v <= 1.0f);
                    }
                    expectGreaterThan (slideHigh - slideLow, 0.3f, what + ": the vowels must move");

                    auto shaped = a;
                    shapeExpression (shaped, expr);
                    juce::String why;
                    expect (channelsAreExclusive (renderMpe (shaped), why), what + ": " + why);
                    expect (renderMono (shaped, 12, true).getNumEvents() > 0);
                }
        {
            // Vowels at 0: the timbre stays put. The sentence ends: a statement falls, a question rises.
            GenParams gp;
            gp.engine = LeadEngine::voice;
            gp.vowels = 0.0f;
            gp.growl = 0.0f;
            gp.inflection = 1.0f;
            gp.form = Form::callResponse; // A (statement) B (question)
            gp.bars = 2;
            const auto flat = generateVoice (gp, expr);
            float lo = 1.0f, hi = 0.0f;
            for (const auto& n : flat.notes)
                for (const auto& pt : n.slide)
                {
                    lo = std::min (lo, pt.v);
                    hi = std::max (hi, pt.v);
                }
            expectLessThan (hi - lo, 0.02f);

            const Note* lastA = nullptr;
            const Note* lastB = nullptr;
            for (const auto& n : flat.notes)
                (n.start < 4.0 ? lastA : lastB) = &n;
            expect (lastA != nullptr && lastB != nullptr);
            if (lastA != nullptr && lastB != nullptr)
            {
                expectLessThan (lastA->bend.back().v, -0.5f, "a statement falls at the end");
                expectGreaterThan (lastB->bend.back().v, 0.5f, "a question rises at the end");
            }
        }

        beginTest ("KIT: the Voice lead gives the Stab its accents");
        {
            KitParams kp;
            kp.gen.engine = LeadEngine::voice;
            kp.layers[(size_t) Layer::stab].pattern = (int) StabPattern::accents;
            const auto parts = generateKit (kp);
            std::set<double> accents;
            size_t stabs = 0;
            for (const auto& part : parts)
            {
                if (part.layer == Layer::lead)
                    for (const auto& n : part.phrase.notes)
                        if (n.accent)
                            accents.insert (std::round (n.start * 4.0) / 4.0);
                if (part.layer == Layer::stab)
                    for (const auto& n : part.phrase.notes)
                    {
                        ++stabs;
                        expect (accents.count (n.start) != 0);
                    }
            }
            expectGreaterThan ((int) stabs, 0);
        }

        beginTest ("Stab: the power chord's fifth is the scale's");
        {
            KitParams kp;
            kp.gen.scale = scales::Scale::locrian; // A Bb C D Eb F G: the fifth over A is Eb
            kp.gen.harmony = HarmonySource::tonic;
            kp.layers[(size_t) Layer::stab].pattern = (int) StabPattern::downbeat;
            for (const auto& part : generateKit (kp))
                if (part.layer == Layer::stab)
                    for (const auto& n : part.phrase.notes)
                        expect (scales::mod (n.pitch, 12) != 4, "E is not in A Locrian");
        }
    }
};

static V2Tests v2Tests;

// ---- MIDI Learn
static Note clipNote (double start, double length, int pitch, float velocity = 0.8f)
{
    Note n;
    n.start = start;
    n.length = length;
    n.pitch = pitch;
    n.velocity = velocity;
    return n;
}

// Chords held for `each` beats, one after the other.
static Phrase heldChords (const std::vector<std::vector<int>>& chords, double each)
{
    Phrase p;
    p.lengthBeats = learnBeats;
    for (size_t i = 0; i < chords.size(); ++i)
        for (int pitch : chords[i])
            p.notes.push_back (clipNote ((double) i * each, each - 0.1, pitch));
    return p;
}

static juce::String symbols (const std::vector<Region>& chords)
{
    juce::String s;
    for (const auto& c : chords)
        s << chordSymbol (c) << " ";
    return s.trim();
}

class MidiLearnTests : public juce::UnitTest
{
public:
    MidiLearnTests() : juce::UnitTest ("DarkMPE MK2: MIDI Learn") {}

    void runTest() override
    {
        const std::vector<std::vector<int>> amFCG { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 55, 64 }, { 43, 55, 59, 62 } };

        beginTest ("Chords: block, broken, under a melody, slash chords, sevenths, power chords, 2-beat chords");
        {
            const auto block = chordsFromClip (heldChords (amFCG, 4.0), learnBeats);
            expectEquals (symbols (block), juce::String ("Am F C G"));
            expectEquals ((int) block.size(), 4);
            for (size_t i = 0; i < block.size(); ++i)
            {
                expectWithinAbsoluteError (block[i].start, (double) i * 4.0, 1.0e-9);
                expectWithinAbsoluteError (block[i].length, 4.0, 1.0e-9);
            }

            // Broken chords in 8ths: root, third, fifth, third.
            Phrase broken;
            for (int bar = 0; bar < 4; ++bar)
            {
                const auto& c = amFCG[(size_t) bar];
                const int order[] = { 0, 1, 2, 1 };
                for (int k = 0; k < 8; ++k)
                    broken.notes.push_back (clipNote (bar * 4.0 + k * 0.5, 0.45, c[(size_t) order[k % 4]]));
            }
            expectEquals (symbols (chordsFromClip (broken, learnBeats)), juce::String ("Am F C G"));

            // The same chords under a melody of 8ths with passing notes.
            auto withMelody = heldChords (amFCG, 4.0);
            const int tune[] = { 76, 74, 72, 71, 72, 74, 76, 77 };
            for (int bar = 0; bar < 4; ++bar)
                for (int k = 0; k < 8; ++k)
                    withMelody.notes.push_back (clipNote (bar * 4.0 + k * 0.5, 0.45, tune[(k + bar) % 8]));
            juce::String roots; // a melody note may colour the chord (F with an E on top: Fmaj7): the roots stay
            for (const auto& c : chordsFromClip (withMelody, learnBeats))
                roots << scales::keyNames[scales::mod (c.pitches.front(), 12)] << " ";
            expectEquals (roots.trim(), juce::String ("A F C G"));

            // Lament bass: slash chords from the lowest note, and a dominant seventh with its leading tone.
            expectEquals (symbols (chordsFromClip (heldChords ({ { 45, 57, 60, 64 }, { 43, 59, 64 }, { 41, 57, 62 }, { 40, 56, 59, 62 } }, 4.0), learnBeats)),
                          juce::String ("Am Em/G Dm/F E7"));

            // Power chords.
            expectEquals (symbols (chordsFromClip (heldChords ({ { 45, 52 }, { 41, 48 }, { 48, 55 }, { 43, 50 } }, 4.0), learnBeats)),
                          juce::String ("A5 F5 C5 G5"));

            // A slow arpeggio (one note per beat): the chords change on the bar lines.
            Phrase slow;
            const int arpeggio[] = { 69, 72, 76, 72, 69, 72, 77, 72, 67, 72, 76, 72, 67, 71, 74, 71 };
            for (int k = 0; k < 16; ++k)
                slow.notes.push_back (clipNote (k * 1.0, 0.9, arpeggio[k]));
            juce::String slowRoots;
            for (const auto& c : chordsFromClip (slow, learnBeats))
                slowRoots << scales::keyNames[scales::mod (c.pitches.front(), 12)] << "@" << juce::String (c.start, 0) << " ";
            expectEquals (slowRoots.trim(), juce::String ("A@0 F@4 C@8 G@12"));

            // A chord ringing through bars of silence does not become the tonic.
            auto sparse = heldChords ({ { 57, 60, 64 }, {}, { 53, 57, 60 }, {} }, 4.0);
            const auto sparseKey = analyseClip (LearnKind::chords, sparse, 9, scales::Scale::naturalMinor);
            expectEquals (sparseKey.key.key, 9);

            // Two chords per bar.
            const auto half = chordsFromClip (heldChords ({ { 57, 60, 64 }, { 50, 57, 62, 65 }, { 52, 56, 59 }, { 57, 60, 64 },
                                                            { 57, 60, 64 }, { 50, 57, 62, 65 }, { 52, 56, 59 }, { 57, 60, 64 } }, 2.0), learnBeats);
            expectEquals (symbols (half), juce::String ("Am Dm E Am Dm E Am"));
            expectWithinAbsoluteError (half[1].start, 2.0, 1.0e-9);

            expect (chordsFromClip (Phrase {}, learnBeats).empty());
        }

        beginTest ("Key and scale: the scale that holds the notes, the tonic the roots point at, else what was set");
        {
            auto keyOf = [] (const Phrase& clip, int key, scales::Scale scale)
            {
                const auto r = analyseClip (LearnKind::chords, clip, key, scale);
                return juce::String (scales::keyNames[r.key.key]) + " " + scales::scaleNames[(size_t) r.key.scale];
            };
            expectEquals (keyOf (heldChords (amFCG, 4.0), 9, scales::Scale::phrygian), juce::String ("A Natural Minor"));
            expectEquals (keyOf (heldChords ({ { 57, 60, 64 }, { 58, 62, 65 }, { 55, 58, 62 }, { 58, 62, 65 } }, 4.0), 9, scales::Scale::naturalMinor),
                          juce::String ("A Phrygian"));
            expectEquals (keyOf (heldChords ({ { 57, 60, 64 }, { 53, 57, 60 }, { 50, 53, 57 }, { 52, 56, 59, 62 } }, 4.0), 9, scales::Scale::naturalMinor),
                          juce::String ("A Harmonic Minor"));
            // Power chords do not say minor or phrygian: the scale already set stays.
            const auto fifths = heldChords ({ { 45, 52 }, { 41, 48 }, { 43, 50 }, { 45, 52 } }, 4.0);
            expectEquals (keyOf (fifths, 9, scales::Scale::phrygian), juce::String ("A Phrygian"));
            expectEquals (keyOf (fifths, 9, scales::Scale::naturalMinor), juce::String ("A Natural Minor"));

            // A D Dorian melody (the B natural, D first, last and lowest).
            Phrase dorian;
            const int line[] = { 62, 64, 65, 67, 69, 71, 72, 69, 67, 65, 64, 62 };
            for (int k = 0; k < 12; ++k)
                dorian.notes.push_back (clipNote (k * 1.0, 0.9, line[k]));
            const auto lead = analyseClip (LearnKind::lead, dorian, 9, scales::Scale::naturalMinor);
            expect (lead.ok);
            expectEquals (lead.key.key, 2);
            expectEquals ((int) lead.key.scale, (int) scales::Scale::dorian);
            expectEquals ((int) lead.events.size(), 12);
        }

        beginTest ("Bass line: one root per half bar, the scale's chords on them");
        {
            Phrase bass;
            const int roots[] = { 45, 41, 48, 43 };
            for (int bar = 0; bar < 4; ++bar)
                for (int s = 0; s < 16; ++s)
                    bass.notes.push_back (clipNote (bar * 4.0 + s * 0.25, 0.2, s == 15 ? roots[bar] + 2 : (s % 4 == 2 ? roots[bar] + 12 : roots[bar])));
            const auto r = analyseClip (LearnKind::bass, bass, 9, scales::Scale::naturalMinor);
            expect (r.ok);
            expectEquals (symbols (r.chords), juce::String ("Am F C G"));
            expectEquals (r.key.key, 9);

            // A root per half bar.
            Phrase halves;
            for (int h = 0; h < 8; ++h)
                halves.notes.push_back (clipNote (h * 2.0, 1.9, h % 2 == 0 ? 45 : 40));
            const auto split = bassRoots (halves, learnBeats);
            expectEquals ((int) split.size(), 8);
            expectEquals (split[1].pc, 4);
        }

        beginTest ("Grid: 1/16, 1/16 triplets, 1/32");
        {
            std::vector<double> straight, triplets, fine;
            for (int k = 0; k < 16; ++k)
            {
                straight.push_back (k * 0.25);
                triplets.push_back (k / 6.0);
                fine.push_back (k * 0.125);
            }
            expectEquals ((int) detectRate (straight), (int) Rate::sixteenth);
            expectEquals ((int) detectRate (triplets), (int) Rate::sixteenthTriplet);
            expectEquals ((int) detectRate (fine), (int) Rate::thirtySecond);
        }

        // A learned song: Am F C G and a lead over it.
        auto learned = std::make_shared<Learned>();
        learned->chords = chordsFromClip (heldChords (amFCG, 4.0), learnBeats);
        const std::vector<LearnedEvent> tune {
            { 0.0, 1.0, 69, 0.9f }, { 1.0, 0.5, 72, 0.7f }, { 1.5, 0.5, 71, 0.7f }, { 2.0, 1.75, 69, 0.8f },
            { 4.0, 0.5, 69, 0.9f }, { 4.5, 0.5, 72, 0.7f }, { 5.0, 2.0, 77, 0.85f }, { 7.0, 0.75, 76, 0.7f },
            { 8.0, 1.5, 76, 0.9f }, { 9.5, 0.5, 74, 0.7f }, { 10.0, 1.0, 72, 0.8f }, { 11.0, 1.0, 67, 0.6f },
            { 12.0, 0.25, 67, 0.9f }, { 12.25, 0.25, 71, 0.7f }, { 12.5, 0.5, 74, 0.7f }, { 13.0, 2.5, 71, 0.8f } };
        learned->lead = tune;

        auto song = [&learned]
        {
            GenParams gp;
            gp.scale = scales::Scale::naturalMinor;
            gp.harmony = HarmonySource::learned;
            gp.motif = MotifSource::lead;
            gp.learned = learned;
            gp.slide = 0.0f;
            return gp;
        };

        beginTest ("Your lead comes back note for note over your chords, and moves with other chords");
        {
            auto gp = song();
            const auto mel = generateMelody (gp);
            expectEquals ((int) mel.notes.size(), (int) tune.size());
            for (size_t i = 0; i < tune.size() && i < mel.notes.size(); ++i)
            {
                expectWithinAbsoluteError (mel.notes[i].start, tune[i].start, 1.0e-9);
                expectEquals (mel.notes[i].pitch, tune[i].pitch);
                expectWithinAbsoluteError (mel.notes[i].length, tune[i].length, 1.0e-9);
                expectWithinAbsoluteError (mel.notes[i].velocity, tune[i].velocity, 1.0e-6f);
            }

            gp.bars = 8; // the idea comes round again
            const auto eight = generateMelody (gp);
            expectEquals ((int) eight.notes.size(), 2 * (int) tune.size());
            for (size_t i = 0; i < tune.size() && i + tune.size() < eight.notes.size(); ++i)
            {
                expectEquals (eight.notes[i + tune.size()].pitch, tune[i].pitch);
                expectWithinAbsoluteError (eight.notes[i + tune.size()].start, tune[i].start + 16.0, 1.0e-9);
            }

            gp = song();
            gp.baseOctave = 2; // Oct Base moves your line by an octave
            const auto low = generateMelody (gp);
            for (size_t i = 0; i < tune.size() && i < low.notes.size(); ++i)
                expectEquals (low.notes[i].pitch, tune[i].pitch - 12);

            // Over the Phrygian Dark progression: same rhythm, every note in the scale, and not the same notes.
            gp = song();
            gp.harmony = HarmonySource::progression;
            gp.progression = Progression::phrygianDark;
            const auto moved = generateMelody (gp);
            expectEquals ((int) moved.notes.size(), (int) tune.size());
            int changed = 0;
            for (size_t i = 0; i < tune.size() && i < moved.notes.size(); ++i)
            {
                expectWithinAbsoluteError (moved.notes[i].start, tune[i].start, 1.0e-9);
                expect (scales::inScale (moved.notes[i].pitch, gp.key, gp.scale));
                changed += moved.notes[i].pitch != tune[i].pitch ? 1 : 0;
            }
            expectGreaterThan (changed, 0);

            // MUTATE keeps your rhythm and bars 1 and 3, and varies the answers.
            int varied = 0;
            for (int v = 1; v <= 6; ++v)
            {
                gp = song();
                gp.variation = v;
                const auto m = generateMelody (gp);
                expectEquals ((int) m.notes.size(), (int) tune.size());
                for (size_t i = 0; i < tune.size() && i < m.notes.size(); ++i)
                {
                    expectWithinAbsoluteError (m.notes[i].start, tune[i].start, 1.0e-9);
                    const bool answer = tune[i].start >= 4.0 && tune[i].start < 8.0 ? true : tune[i].start >= 12.0;
                    if (! answer)
                        expectEquals (m.notes[i].pitch, tune[i].pitch, "MUTATE must keep bars 1 and 3");
                    varied += answer && m.notes[i].pitch != tune[i].pitch ? 1 : 0;
                }
            }
            expectGreaterThan (varied, 0);

            // A form takes your first bar as the idea A (the same notes over the same chord).
            gp = song();
            gp.form = Form::abac;
            gp.harmony = HarmonySource::tonic;
            gp.learned = std::make_shared<Learned> (Learned { {}, tune, {} });
            const auto form = generateMelody (gp);
            std::vector<std::pair<long, int>> a0, a2, first;
            for (const auto& n : form.notes)
            {
                if (n.start < 4.0) a0.push_back ({ std::lround (n.start * 1000.0), n.pitch });
                if (n.start >= 8.0 && n.start < 12.0) a2.push_back ({ std::lround ((n.start - 8.0) * 1000.0), n.pitch });
            }
            for (const auto& e : tune)
                if (e.start < 4.0)
                    first.push_back ({ std::lround (e.start * 1000.0), e.pitch });
            expect (a0 == first, "A is your first bar");
            expect (a0 == a2, "A must come back");
        }

        beginTest ("Your chromatic notes: kept without Scale Lock, in the scale with it");
        {
            auto chromatic = std::make_shared<Learned> (*learned);
            chromatic->lead[2].pitch = 70; // A# over Am
            auto gp = song();
            gp.learned = chromatic;
            gp.scaleLock = false;
            expectEquals (generateMelody (gp).notes[2].pitch, 70);
            gp.scaleLock = true;
            expect (scales::inScale (generateMelody (gp).notes[2].pitch, gp.key, gp.scale));
        }

        beginTest ("Your rhythm: the riff (and the voice) sounds where you played");
        {
            auto rhythmic = std::make_shared<Learned>();
            for (int bar = 0; bar < 4; ++bar)
                for (double t : { 0.0, 0.75, 1.5, 2.0, 2.75, 3.5 })
                    if (! (bar == 3 && t > 3.0))
                        rhythmic->rhythm.push_back ({ bar * 4.0 + t + (bar == 1 ? 0.25 : 0.0) * (t > 0.0 ? 1.0 : 0.0), 0.2, -1, t == 0.0 ? 1.0f : 0.7f });
            std::vector<long> want;
            for (const auto& e : rhythmic->rhythm)
                want.push_back (std::lround (e.start * 1000.0));

            for (int style = 0; style < (int) Style::count; ++style)
            {
                GenParams gp;
                gp.style = (Style) style;
                gp.motif = MotifSource::rhythm;
                gp.learned = rhythmic;
                gp.density = style % 2 == 0 ? 0.0f : 1.0f; // your rhythm, whatever the density
                std::vector<long> got;
                for (const auto& n : generateMelody (gp).notes)
                    got.push_back (std::lround (n.start * 1000.0));
                expect (got == want, juce::String (styleNames[style]) + ": not your rhythm");
            }

            GenParams gv;
            gv.engine = LeadEngine::voice;
            gv.motif = MotifSource::rhythm;
            gv.learned = rhythmic;
            std::vector<long> spoken;
            for (const auto& n : generateLead (gv, {}).notes)
                spoken.push_back (std::lround (n.start * 1000.0));
            expect (spoken == want, "the voice must speak on your rhythm");
        }

        beginTest ("Voice speaks your lead: your notes, with vowels and inflection");
        {
            auto gp = song();
            gp.engine = LeadEngine::voice;
            const auto spoken = generateLead (gp, {});
            expectEquals ((int) spoken.notes.size(), (int) tune.size());
            float lo = 1.0f, hi = 0.0f;
            for (size_t i = 0; i < tune.size() && i < spoken.notes.size(); ++i)
            {
                expectWithinAbsoluteError (spoken.notes[i].start, tune[i].start, 1.0e-9);
                expectEquals (spoken.notes[i].pitch, tune[i].pitch);
                expect (spoken.notes[i].lockedExpr);
                for (const auto& pt : spoken.notes[i].slide)
                {
                    lo = std::min (lo, pt.v);
                    hi = std::max (hi, pt.v);
                }
            }
            expectGreaterThan (hi - lo, 0.3f);
            expectLessOrEqual (maxPolyphony (spoken), 1);
        }

        beginTest ("Harmony MIDI In: the KIT follows your chords, borrowed chords are kept, the pad plays them");
        {
            auto dominant = std::make_shared<Learned>();
            dominant->chords = chordsFromClip (heldChords ({ { 57, 60, 64 }, { 53, 57, 60 }, { 50, 53, 57 }, { 52, 56, 59, 62 } }, 4.0), learnBeats);
            KitParams kp;
            kp.gen.scale = scales::Scale::naturalMinor;
            kp.gen.harmony = HarmonySource::learned;
            kp.gen.learned = dominant;
            kp.pad.motion = Motion::pulse; // chords struck again: the pad's notes are the chord tones
            kp.pad.reharm = Reharm::off;
            kp.pad.tension = 0.0f;
            kp.gen.scaleLock = true;
            kp.gen.bars = 8;
            const auto track = leadHarmony (kp.gen);
            expectEquals ((int) track.spans.size(), 8);
            expectEquals (juce::String (chordSymbol (track.spans[7].chord)), juce::String ("E7"));
            expectEquals ((int) track.spans[3].scale, (int) scales::Scale::harmonicMinor);
            expect (! track.inKey());

            bool padLeadingTone = false;
            for (const auto& part : generateKit (kp))
            {
                if (part.layer == Layer::pad)
                    for (const auto& n : part.phrase.notes)
                        padLeadingTone = padLeadingTone || (scales::mod (n.pitch, 12) == 8 && n.start >= 12.0 - 1.0 && n.start < 16.0);
                if (part.layer == Layer::bass)
                    for (const auto& n : part.phrase.notes)
                        if (n.start >= 12.0 && n.start < 16.0 && (int) std::floor (n.start) % 4 == 0)
                            expectEquals (scales::mod (n.pitch, 12), 4, "the bass plays E under E7");
            }
            expect (padLeadingTone, "the pad keeps your G#");

            // Nothing learned: MIDI In is the tonic.
            GenParams empty;
            empty.harmony = HarmonySource::learned;
            expectEquals ((int) leadHarmony (empty).spans.size(), 1);
        }

        beginTest ("Nothing heard");
        {
            const auto r = analyseClip (LearnKind::chords, Phrase {}, 9, scales::Scale::phrygian);
            expect (! r.ok);
            expect (! r.message.empty());
        }
    }
};

static MidiLearnTests midiLearnTests;

static EngineTests engineTests;
static CinematicTests cinematicTests;
static MpeImportTests mpeImportTests;

// DarkMPETests --render <inDir> <outDir>: demo clips. Every lead style (MPE and mono), cinematic showcases,
// a whole kit (one file per layer), and for every .mid in inDir: all voicings plus cinematic versions.
static int renderExamples (const juce::File& inDir, const juce::File& outDir)
{
    outDir.createDirectory();

    auto write = [&] (const juce::MidiMessageSequence& seq, const juce::String& name, size_t notes)
    {
        const auto f = outDir.getChildFile (juce::File::createLegalFileName (name) + ".mid");
        writeMidiFile (makeMidiFile (seq, 125.0, name), f);
        std::cout << "wrote " << f.getFileName() << "  (" << notes << " notes)" << std::endl;
    };
    auto save = [&] (Phrase p, const juce::String& name)
    {
        shapeExpression (p, {});
        write (renderMpe (p), name, p.notes.size());
    };

    for (int s = 0; s < (int) Style::count; ++s)
    {
        GenParams gp;
        gp.style = (Style) s;
        gp.seed = 666 + s;
        auto lead = generateMelody (gp);
        save (lead, juce::String ("Lead - ") + styleNames[s]);
        shapeExpression (lead, {});
        write (renderMono (lead, 12, true), juce::String ("Lead - ") + styleNames[s] + " (Mono, bend 12)", lead.notes.size());
    }

    // V2: the Voice engine over the Epic Minor progression (A B A C: statement, question, statement, close),
    // a riff that follows a progression with long notes, and a triplet grid.
    for (int v = 0; v < (int) VoiceStyle::count; ++v)
    {
        GenParams gp;
        gp.engine = LeadEngine::voice;
        gp.voice = (VoiceStyle) v;
        gp.seed = 777 + v;
        gp.form = Form::abac;
        gp.harmony = HarmonySource::progression;
        gp.progression = (VoiceStyle) v == VoiceStyle::titan ? Progression::lamentBass : Progression::epicMinor;
        gp.baseOctave = (VoiceStyle) v == VoiceStyle::titan ? 2 : 3;
        ExprParams ep;
        ep.vibratoDepth = 0.25f;
        auto lead = generateLead (gp, ep);
        save (lead, juce::String ("Voice - ") + voiceStyleNames[v]);
        shapeExpression (lead, ep);
        write (renderMono (lead, 12, true), juce::String ("Voice - ") + voiceStyleNames[v] + " (Mono, bend 12)", lead.notes.size());
    }
    {
        GenParams gp;
        gp.seed = 666;
        gp.harmony = HarmonySource::progression;
        gp.progression = Progression::harmonicDominant;
        gp.longNotes = 0.4f;
        save (generateMelody (gp), "Lead - Pursuit over Harmonic Dominant, long notes");
        gp = {};
        gp.seed = 667;
        gp.style = Style::acidSlide;
        gp.rate = Rate::sixteenthTriplet;
        gp.longNotes = 0.25f;
        save (generateMelody (gp), "Lead - Acid Slide 1-16T");
    }

    // Phrase forms: the same seed as classic, as A B A C / period / sentence / sequence (8 bars).
    for (auto form : { Form::abac, Form::period, Form::sentence, Form::sequence, Form::aaab })
    {
        GenParams gp;
        gp.style = Style::pursuit;
        gp.seed = 666;
        gp.bars = 8;
        gp.form = form;
        save (generateMelody (gp), juce::String ("Form - Lead Pursuit - ") + formNames[(int) form]);

        HarmonyParams hp;
        hp.bars = 8;
        hp.form = form;
        CineParams cp;
        cp.reharm = Reharm::suspensions;
        cp.tension = 0.3f;
        cp.sub = true;
        std::vector<Region> used;
        auto phrase = cinematicRegions (generateProgression (hp), 32.0, cp, hp.key, &used);
        juce::String chords;
        for (const auto& r : used)
            chords << " " << chordSymbol (r);
        std::cout << "  " << formNames[(int) form] << ":" << chords << std::endl;
        save (phrase, juce::String ("Form - Cinematic Epic Minor - ") + formNames[(int) form]);
    }

    struct Showcase { const char* name; Progression prog; Motion motion; Reharm reharm; VoicingMode voicing; GlideShape shape;
                      float tension, darkness, fall; bool sub; ChordLength len; };
    const Showcase shows[] = {
        { "Epic Minor - Morph",            Progression::epicMinor,        Motion::morph,       Reharm::susResolve,        VoicingMode::epicSpread,  GlideShape::ease,     0.35f, 0.4f, 0.0f, true,  ChordLength::oneBar },
        { "Mediant Chain - Bloom",         Progression::mediantChain,     Motion::bloom,       Reharm::off,               VoicingMode::gothic,      GlideShape::swoopOut, 0.5f,  0.8f, 0.0f, false, ChordLength::oneBar },
        { "Tritone Abyss - Deep Note",     Progression::tritoneAbyss,     Motion::deepNote,    Reharm::off,               VoicingMode::hyperSpread, GlideShape::ease,     0.3f,  0.9f, 0.0f, true,  ChordLength::twoBars },
        { "Lament Bass - Suspensions",     Progression::lamentBass,       Motion::morph,       Reharm::suspensions,       VoicingMode::epicSpread,  GlideShape::stepped,  0.25f, 0.6f, 0.0f, true,  ChordLength::oneBar },
        { "Phrygian Dark - Pulse",         Progression::phrygianDark,     Motion::pulse,       Reharm::off,               VoicingMode::gothic,      GlideShape::swoopOut, 0.2f,  0.9f, 0.5f, true,  ChordLength::oneBar },
        { "Tonic Pedal - Tension Rise",    Progression::tonicPedal,       Motion::tensionRise, Reharm::tonicPedal,        VoicingMode::epicSpread,  GlideShape::swoopIn,  0.6f,  0.7f, 0.0f, true,  ChordLength::oneBar },
        { "Line Cliche - Morph",           Progression::lineCliche,       Motion::morph,       Reharm::off,               VoicingMode::epicSpread,  GlideShape::linear,   0.1f,  0.3f, 0.0f, false, ChordLength::oneBar },
        { "Harmonic Dominant - Breathe",   Progression::harmonicDominant, Motion::breathe,     Reharm::chromaticApproach, VoicingMode::hyperSpread, GlideShape::ease,     0.4f,  0.6f, 0.4f, false, ChordLength::oneBar },
        { "Auto - Planing Morph",          Progression::autoSeed,         Motion::morph,       Reharm::planing,           VoicingMode::gothic,      GlideShape::swoopOut, 0.3f,  0.8f, 0.0f, true,  ChordLength::oneBar },
        { "Neapolitan - Tritone Approach", Progression::neapolitan,       Motion::morph,       Reharm::tritoneApproach,   VoicingMode::epicSpread,  GlideShape::ease,     0.3f,  0.7f, 0.3f, true,  ChordLength::oneBar },
    };
    int index = 1;
    for (const auto& s : shows)
    {
        HarmonyParams hp;
        hp.key = 9;
        hp.progression = s.prog;
        hp.chordLength = s.len;
        hp.bars = s.len == ChordLength::twoBars ? 8 : 4;
        hp.darkness = s.darkness;
        hp.seed = 7;
        CineParams cp;
        cp.motion = s.motion;
        cp.reharm = s.reharm;
        cp.voicing = s.voicing;
        cp.shape = s.shape;
        cp.tension = s.tension;
        cp.darkness = s.darkness;
        cp.fall = s.fall;
        cp.sub = s.sub;
        cp.arc = 0.4f;
        cp.seed = 7;
        std::vector<Region> used;
        auto phrase = cinematicRegions (generateProgression (hp), hp.bars * 4.0, cp, hp.key, &used);
        juce::String chords;
        for (const auto& r : used)
            chords << " " << chordSymbol (r);
        std::cout << "  " << s.name << ":" << chords << std::endl;
        save (phrase, "Cinematic - " + juce::String (index++).paddedLeft ('0', 2) + " " + s.name);
    }

    KitParams kp;
    kp.gen.seed = 1312;
    kp.gen.scale = scales::Scale::phrygian;
    kp.layers[(size_t) Layer::siren].on = true;
    kp.pad.motion = Motion::morph;
    kp.pad.tension = 0.35f;
    for (auto& part : generateKit (kp))
    {
        shapeExpression (part.phrase, {});
        if (part.layer == Layer::bass)
            write (renderMono (part.phrase, 12, true), "Kit - Pursuit A Phrygian - Bass (Mono, bend 12)", part.phrase.notes.size());
        else
            write (renderMpe (part.phrase), juce::String ("Kit - Pursuit A Phrygian - ") + layerNames[(int) part.layer], part.phrase.notes.size());
    }

    for (const auto& f : inDir.findChildFiles (juce::File::findFiles, false, "*.mid"))
    {
        Phrase src;
        juce::String err;
        if (! loadMidiFile (f, src, err))
            continue;
        const auto base = f.getFileNameWithoutExtension();
        if (! isMonophonic (src))
            for (auto motion : { Motion::morph, Motion::bloom, Motion::pulse })
                for (auto reharm : { Reharm::off, Reharm::suspensions })
                {
                    CineParams cp;
                    cp.motion = motion;
                    cp.reharm = reharm;
                    cp.tension = 0.3f;
                    cp.shape = motion == Motion::morph ? GlideShape::ease : GlideShape::swoopOut;
                    save (cinematic (src, cp), "Cinematic - " + base + " - " + motionNames[(int) motion] + " - " + reharmNames[(int) reharm]);
                }
        for (int m = 0; m < (int) VoicingMode::count; ++m)
        {
            VoicingParams vp;
            vp.mode = (VoicingMode) m;
            vp.strum = 0.02;
            save (applyVoicing (src, vp), base + " - " + voicingNames[m]);
        }
    }
    return 0;
}

// DarkMPETests --inspect <file.mid>: how the importer reads a file.
static int inspect (const juce::File& f)
{
    Phrase p;
    LoadInfo li;
    juce::String err;
    if (! loadMidiFile (f, p, err, &li))
    {
        std::cout << "ERROR: " << err << std::endl;
        return 1;
    }
    std::cout << f.getFileName() << ": " << (li.mpe ? "MPE" : "plain MIDI") << ", channels " << li.channels
              << ", notes " << li.notes << ", with expression " << li.notesWithExpression
              << ", bend range " << li.bendRange << ", " << (isMonophonic (p) ? "melody" : "chords")
              << ", length " << p.lengthBeats << " beats" << std::endl;
    for (const auto& c : detectChords (p))
    {
        std::cout << "  @" << juce::String (c.start, 2) << " len " << juce::String (c.length, 2) << "  [";
        for (int x : c.pitches) std::cout << " " << x;
        std::cout << " ]" << std::endl;
    }
    return 0;
}

int main (int argc, char** argv)
{

    if (argc == 3 && juce::String (argv[1]) == "--inspect")
        return inspect (juce::File (argv[2]));

    if (argc == 4 && juce::String (argv[1]) == "--render")
        return renderExamples (juce::File (argv[2]), juce::File (argv[3]));

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTests ({ &engineTests, &mpeImportTests, &cinematicTests, &harmonyTests, &kitTests, &formTests, &renderTests, &v2Tests, &midiLearnTests });

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}

#include <juce_audio_basics/juce_audio_basics.h>

#include "engine/CinematicEngine.h"
#include "engine/HarmonyEngine.h"
#include "engine/Humanize.h"
#include "engine/KitGenerator.h"
#include "engine/ExpressionShaper.h"
#include "engine/GestureEngine.h"
#include "engine/MelodyGenerator.h"
#include "engine/MidiFileIO.h"
#include "engine/MpeRenderer.h"
#include "engine/VoicingEngine.h"

#include <map>
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

class ChordTests : public juce::UnitTest
{
public:
    ChordTests() : juce::UnitTest ("DarkMPE chords") {}

    void runTest() override
    {
        beginTest ("Auto progression: tonic first, every phrase ends on a dominant, no chord twice in a row");
        for (int seed = 1; seed <= 40; ++seed)
            for (float darkness : { 0.0f, 0.5f, 1.0f })
                for (auto len : { ChordLength::oneBar, ChordLength::twoBeats })
                {
                    HarmonyParams hp;
                    hp.progression = Progression::autoSeed;
                    hp.bars = 8;
                    hp.chordLength = len;
                    hp.seed = seed;
                    hp.darkness = darkness;
                    hp.scale = seed % 2 == 0 ? scales::Scale::naturalMinor : scales::Scale::phrygian;
                    const auto rs = generateProgression (hp);
                    const juce::String what = "seed " + juce::String (seed) + " darkness " + juce::String (darkness);
                    expectEquals (juce::String (chordSymbol (rs.front())), juce::String ("Am"), what);

                    // dominants of A minor: v / V (E), bVII (G), vii dim (G#), bII (Bb), #iv (D#), iii (C#)
                    const std::set<int> dominants { 4, 7, 8, 10, 3, 1 };
                    auto rootPc = [&] (const Region& r) { return scales::mod (r.pitches.front(), 12); };
                    expect (dominants.count (rootPc (rs.back())) != 0, what + ": the loop must lead home, got " + chordSymbol (rs.back()));
                    if (rs.size() >= 8)
                        expect (dominants.count (rootPc (rs[rs.size() / 2 - 1])) != 0, what + ": half cadence");
                    for (size_t i = 1; i < rs.size(); ++i)
                        expect (rs[i].pitches != rs[i - 1].pitches || rs[i].bassPc != rs[i - 1].bassPc, what + ": repeated chord");
                }

        beginTest ("Colours belong to the chord: a chord that comes back keeps its colours");
        {
            HarmonyParams hp;
            hp.progression = Progression::mediantChain; // i bvi i iii
            auto rs = generateProgression (hp);
            colourRegions (rs, 1.0f, 0.6f, 9);
            expect (rs[0].pitches == rs[2].pitches, "both i chords must get the same colours");
            expect (rs[0].pitches.size() > 3);
        }

        beginTest ("The loop comes round: every morphing voice arrives on its first note");
        for (auto motion : { Motion::morph, Motion::counterline, Motion::ripple, Motion::shimmer })
        {
            HarmonyParams hp;
            hp.progression = Progression::epicMinor;
            CineParams cp;
            cp.motion = motion;
            cp.reharm = Reharm::off;
            cp.stagger = 0.0f;
            cp.voices = 4;
            const auto out = cinematicRegions (generateProgression (hp), 16.0, cp, hp.key);
            std::map<int, float> first, last;
            for (const auto& n : out.notes)
            {
                if (n.start < 1.0e-9)
                    first[n.voiceIndex] = (float) n.pitch + evalCurve (n.bend, 0.0, 0.0f);
                if (n.end() > 16.0 - 1.0e-6)
                    last[n.voiceIndex] = (float) n.pitch + evalCurve (n.bend, n.length - 0.004, 0.0f);
            }
            expectEquals ((int) first.size(), (int) last.size());
            for (const auto& [k, pitch] : first)
                expectWithinAbsoluteError (last[k], pitch, 0.35f, juce::String (motionNames[(int) motion]) + ": voice " + juce::String (k) + " must glide into the loop start");
        }

        beginTest ("Counterline sings, Ripple dips every voice, Shimmer spreads the voices");
        {
            HarmonyParams hp;
            hp.progression = Progression::harmonicDominant;
            CineParams cp;
            cp.reharm = Reharm::off;
            cp.voices = 5;
            cp.stagger = 0.0f;
            auto plain = cinematicRegions (generateProgression (hp), 16.0, cp, hp.key);
            auto voiceAt = [] (const Phrase& ph, int k, double t)
            {
                for (const auto& n : ph.notes)
                    if (n.voiceIndex == k && t >= n.start && t < n.end())
                        return (float) n.pitch + evalCurve (n.bend, t - n.start, 0.0f);
                return 0.0f;
            };

            cp.motion = Motion::counterline;
            const auto counter = cinematicRegions (generateProgression (hp), 16.0, cp, hp.key);
            std::set<int> offsets;
            for (double t = 4.0; t < 8.0; t += 0.05)
                offsets.insert ((int) std::lround (voiceAt (counter, 4, t) - voiceAt (plain, 4, t)));
            expectGreaterOrEqual ((int) offsets.size(), 2, "the top voice must move by steps");

            cp.motion = Motion::ripple;
            const auto ripple = cinematicRegions (generateProgression (hp), 16.0, cp, hp.key);
            for (int k = 0; k < 5; ++k)
            {
                float lowest = 0.0f;
                for (double t = 0.0; t < 4.0; t += 0.02)
                    lowest = std::min (lowest, voiceAt (ripple, k, t) - voiceAt (plain, k, t));
                expect (lowest <= -0.9f && lowest >= -2.1f, "voice " + juce::String (k) + " must dip a scale step");
            }

            cp.motion = Motion::shimmer;
            const auto shimmer = cinematicRegions (generateProgression (hp), 16.0, cp, hp.key);
            float spreadTop = 0.0f, spreadBottom = 0.0f;
            for (double t = 2.0; t < 3.2; t += 0.05)
            {
                spreadTop += voiceAt (shimmer, 4, t) - voiceAt (plain, 4, t);
                spreadBottom += voiceAt (shimmer, 0, t) - voiceAt (plain, 0, t);
            }
            expectGreaterThan (spreadTop, 0.0f, "the top voice drifts up");
            expectLessThan (spreadBottom, 0.0f, "the bottom voice drifts down");
        }
    }
};

static ChordTests chordTests;

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
    // The moving voice of one bar (pedal notes follow the chord, so they may differ between sections).
    static std::vector<Note> melodicNotes (const Phrase& p, int b)
    {
        std::vector<Note> v;
        for (const auto& n : p.notes)
            if (! n.pedal && n.start >= b * 4.0 - 1.0e-9 && n.start < b * 4.0 + 4.0 - 1.0e-9)
                v.push_back (n);
        return v;
    }
    static std::vector<std::pair<long, int>> melodic (const Phrase& p, int b)
    {
        std::vector<std::pair<long, int>> v;
        for (const auto& n : melodicNotes (p, b))
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

        beginTest ("Every style: A comes back note for note, B ends on the fifth, C closes on the tonic");
        for (int st = 0; st < (int) Style::count; ++st)
            for (int sc : { (int) scales::Scale::phrygian, (int) scales::Scale::harmonicMinor, (int) scales::Scale::minorPentatonic })
            {
                GenParams gp;
                gp.style = (Style) st;
                gp.scale = (scales::Scale) sc;
                gp.form = Form::abac;
                gp.bars = 4;
                gp.chroma = 0.3f; // chromatic approaches come back with A too
                gp.seed = 99 + st;
                const auto mel = generateMelody (gp);
                const juce::String what = juce::String (styleNames[st]) + " / " + scales::scaleNames[sc];
                expect (melodic (mel, 0) == melodic (mel, 2), what + ": A must repeat note for note (moving voice)");
                expect (bar (mel, 1) != bar (mel, 3), what + ": B and C must differ");
                expect (rhythm (mel, 0).front() == rhythm (mel, 1).front(), what + ": the answer starts like the call");

                const auto& last = mel.notes.back();
                expectEquals (scales::mod (last.pitch - gp.key, 12), 0, what + ": C must end on the key's tonic");
                expect (mel.lengthBeats - last.end() < 0.05, what + ": the closing note is held");
                expect (isMonophonic (mel), what);

                // B lands open on the key's fifth.
                const auto b = melodicNotes (mel, 1);
                if (! b.empty())
                {
                    const int fifth = scales::degreeToPitch (0, gp.scale, scales::mapDegree (gp.scale, 4));
                    expectEquals (scales::mod (b.back().pitch - gp.key, 12), scales::mod (fifth, 12), what + ": B must end on the fifth");
                }

                // MUTATE re-draws the answers, never A.
                auto mutated = gp;
                mutated.variation = 1;
                const auto m2 = generateMelody (mutated);
                expect (bar (m2, 0) == bar (mel, 0), what + ": MUTATE must keep A");
                expect (bar (m2, 2) == bar (mel, 2), what + ": MUTATE must keep the return of A");
            }

        beginTest ("Period: B' closes on the tonic; repeated A has identical expression");
        {
            GenParams gp;
            gp.form = Form::period;
            gp.style = Style::pursuit;
            gp.seed = 31;
            gp.chroma = 0.0f;
            auto mel = generateMelody (gp);
            const auto closed = melodicNotes (mel, 3);
            expect (! closed.empty());
            if (! closed.empty())
                expectEquals (scales::mod (closed.back().pitch - gp.key, 12), 0, "B' must end on the tonic");

            humanize (mel, 1.0f, 5);
            shapeExpression (mel, {});
            const auto a0 = melodicNotes (mel, 0), a2 = melodicNotes (mel, 2);
            expectEquals ((int) a0.size(), (int) a2.size());
            bool same = a0.size() == a2.size();
            for (size_t i = 0; same && i < a0.size(); ++i)
            {
                same = std::abs ((a0[i].start + 8.0) - a2[i].start) < 1.0e-9 && a0[i].velocity == a2[i].velocity;
                // What comes before / after the section may tie into it differently: compare the untied notes.
                if (a0[i].glideFrom != a2[i].glideFrom || std::abs (a0[i].length - a2[i].length) > 1.0e-9)
                    continue;
                same = same && a0[i].bend.size() == a2[i].bend.size();
                for (size_t k = 0; same && k < a0[i].bend.size(); ++k)
                    same = std::abs (a0[i].bend[k].v - a2[i].bend[k].v) < 1.0e-6f;
            }
            expect (same, "a repeated A must be humanized and shaped identically");
        }

        beginTest ("Sequence: A+ is A one scale step up, contour intact");
        for (int st = 0; st < (int) Style::count; ++st)
        {
            GenParams gp;
            gp.form = Form::sequence;
            gp.style = (Style) st;
            gp.chroma = 0.0f;
            gp.seed = 400 + st;
            const auto mel = generateMelody (gp);
            const auto a = melodic (mel, 0), up = melodic (mel, 1);
            const juce::String what = styleNames[st];
            expectEquals ((int) a.size(), (int) up.size(), what);
            if (a.size() != up.size())
                continue;
            for (size_t i = 0; i < a.size(); ++i)
            {
                int next = a[i].second + 1;
                while (! scales::inScale (next, gp.key, gp.scale))
                    ++next;
                expectEquals (scales::mod (up[i].second, 12), scales::mod (next, 12), what + ": every note one step up");
            }
            // The section moves as a whole: the intervals between the notes keep their direction.
            int kept = 0;
            for (size_t i = 1; i < a.size(); ++i)
                kept += ((a[i].second > a[i - 1].second) == (up[i].second > up[i - 1].second)) ? 1 : 0;
            expectGreaterOrEqual (kept, (int) a.size() - 2, what + ": contour");
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

        beginTest ("Forms never change the chords");
        {
            KitParams kp;
            kp.gen.style = Style::pursuit;
            const auto classic = generateKit (kp);
            kp.gen.form = Form::abac;
            const auto abac = generateKit (kp);
            for (size_t i = 0; i < classic.size(); ++i)
                if (classic[i].layer == Layer::pad || classic[i].layer == Layer::stab)
                {
                    const auto& x = classic[i].phrase.notes;
                    const auto& y = abac[i].phrase.notes;
                    bool same = x.size() == y.size();
                    for (size_t k = 0; same && k < x.size(); ++k)
                        same = x[k].pitch == y[k].pitch && std::abs (x[k].start - y[k].start) < 1.0e-9;
                    expect (same, juce::String (layerNames[(int) classic[i].layer]) + " must not follow the form");
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

            // Whatever the chord of the last bar, bass and arp close on the key's tonic.
            for (int st = 0; st < (int) Style::count; ++st)
            {
                kp.gen.style = (Style) st;
                for (const auto& part : generateKit (kp))
                    if (part.layer == Layer::arp || part.layer == Layer::bass)
                        expectEquals (scales::mod (part.phrase.notes.back().pitch - kp.gen.key, 12), 0,
                                      juce::String (styleNames[st]) + " / " + layerNames[(int) part.layer] + ": must close on the tonic");
            }
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

class ExpressionTests : public juce::UnitTest
{
public:
    ExpressionTests() : juce::UnitTest ("DarkMPE expression") {}

    static Note single (double start, double length, int pitch, uint64_t seed)
    {
        Note n;
        n.start = start;
        n.length = length;
        n.pitch = pitch;
        n.exprSeed = seed;
        return n;
    }

    void runTest() override
    {
        beginTest ("Vibrato: a 1-beat note sings at the set depth, 16ths don't");
        {
            Phrase p;
            p.lengthBeats = 4.0;
            p.notes = { single (0.0, 2.0, 60, 11), single (2.0, 1.0, 64, 13), single (3.0, 0.2, 62, 12) };
            ExprParams ep;
            ep.detuneCents = 0.0f;
            ep.vibratoDepth = 0.3f;
            shapeExpression (p, ep);
            auto swing = [] (const Note& n)
            {
                float lo = 0.0f, hi = 0.0f;
                for (const auto& pt : n.bend)
                {
                    lo = std::min (lo, pt.v);
                    hi = std::max (hi, pt.v);
                }
                return std::pair { lo, hi };
            };
            const auto [lo, hi] = swing (p.notes[0]);
            expectWithinAbsoluteError (hi, 0.3f, 0.03f, "vibrato must reach its depth");
            expectWithinAbsoluteError (lo, -0.3f, 0.03f, "vibrato must reach its depth");
            const auto [lo1, hi1] = swing (p.notes[1]);
            expectGreaterThan (hi1 - lo1, 0.3f, "a 1-beat note must sing too");
            for (const auto& pt : p.notes[2].bend)
                expectWithinAbsoluteError (pt.v, 0.0f, 1.0e-6f, "a 16th has no vibrato");
        }

        beginTest ("Detune: every note of a single line has its own offset and drift, within the setting");
        {
            Phrase p;
            p.lengthBeats = 16.0;
            for (int i = 0; i < 16; ++i)
                p.notes.push_back (single (i, 0.9, 60, 100 + (uint64_t) i));
            ExprParams ep;
            ep.detuneCents = 20.0f;
            ep.vibratoDepth = 0.0f;
            shapeExpression (p, ep);
            std::set<int> offsets;
            float widest = 0.0f, drift = 0.0f;
            for (const auto& n : p.notes)
            {
                offsets.insert (juce::roundToInt (n.bend.front().v * 1000.0f));
                float lo = 1.0f, hi = -1.0f;
                for (const auto& pt : n.bend)
                {
                    widest = std::max (widest, std::abs (pt.v));
                    lo = std::min (lo, pt.v);
                    hi = std::max (hi, pt.v);
                }
                drift = std::max (drift, hi - lo);
            }
            expectGreaterThan ((int) offsets.size(), 10, "notes must be detuned differently");
            expectLessOrEqual (widest, 0.2f * 0.5f + 0.2f * 0.3f + 1.0e-4f, "detune stays within the setting");
            expectGreaterThan (drift, 0.01f, "detune drifts inside a note");

            // The same seed gives the same detune (repeated sections).
            Phrase q;
            q.lengthBeats = 16.0;
            q.notes = { single (8.0, 0.9, 60, 100) };
            shapeExpression (q, ep);
            expectWithinAbsoluteError (q.notes[0].bend.front().v, p.notes[0].bend.front().v, 1.0e-6f);
        }

        beginTest ("Gesture curves are layered on the generated expression");
        {
            Phrase p;
            p.lengthBeats = 4.0;
            auto n = single (0.0, 1.0, 60, 7);
            n.gesture = { { 0.0, 0.0f }, { 0.25, 0.0f }, { 0.3, -2.0f }, { 0.6, -2.0f }, { 0.65, 0.0f } };
            n.gestureTimbre = { { 0.0, 0.3f }, { 1.0, 0.0f } };
            n.vibrato = 0.0f;
            p.notes = { n };
            ExprParams ep;
            ep.detuneCents = 0.0f;
            shapeExpression (p, ep);
            const auto& out = p.notes[0];
            expectWithinAbsoluteError (evalCurve (out.bend, 0.45, 0.0f), -2.0f, 1.0e-4f, "the dip must be in the bend");
            expectWithinAbsoluteError (evalCurve (out.bend, 0.9, 0.0f), 0.0f, 1.0e-4f, "and come back");
            bool hasCorner = false;
            for (const auto& pt : out.bend)
                hasCorner = hasCorner || std::abs (pt.t - 0.3) < 1.0e-9;
            expect (hasCorner, "gesture points must be kept exactly");
        }
    }
};

static ExpressionTests expressionTests;

class GestureTests : public juce::UnitTest
{
public:
    GestureTests() : juce::UnitTest ("DarkMPE gestures") {}

    static Phrase longNotes (int count, double length)
    {
        Phrase p;
        p.lengthBeats = count;
        for (int i = 0; i < count; ++i)
        {
            Note n;
            n.start = i;
            n.length = length;
            n.pitch = 57 + (i % 5) * 2;
            n.exprSeed = 900 + (uint64_t) i;
            p.notes.push_back (n);
        }
        return p;
    }

    static std::set<int> kinds (const Phrase& p)
    {
        std::set<int> k;
        for (const auto& n : p.notes)
            k.insert (n.gestureKind);
        return k;
    }

    void runTest() override
    {
        beginTest ("Classic changes nothing");
        {
            GenParams gp;
            gp.bars = 8;
            const auto plain = generateMelody (gp);
            auto shaped = plain;
            GestureParams g;
            g.profile = GestureProfile::classic;
            g.amount = 1.0f;
            g.riff = 1.0f;
            applyGestures (shaped, g, GestureRole::lead);
            expectEquals ((int) shaped.notes.size(), (int) plain.notes.size());
            for (const auto& n : shaped.notes)
                expect (n.gesture.empty() && n.gestureKind == 0 && n.articulation == 0);
        }

        beginTest ("Every profile x style x role: deterministic, in the loop, within Depth, valid MPE");
        for (int prof = 0; prof < (int) GestureProfile::count; ++prof)
            for (int st = 0; st < (int) Style::count; ++st)
                for (auto role : { GestureRole::lead, GestureRole::bass, GestureRole::arp })
                {
                    GenParams gp;
                    gp.style = (Style) st;
                    gp.bars = 4;
                    gp.slide = 0.5f;
                    gp.seed = 70 + prof * 7 + st;
                    GestureParams g;
                    g.profile = (GestureProfile) prof;
                    g.style = gp.style;
                    g.amount = 0.9f;
                    g.depth = (float) st / (float) Style::count;
                    g.riff = 0.8f;
                    g.seed = gp.seed;
                    auto a = generateMelody (gp);
                    auto b = a;
                    applyGestures (a, g, role);
                    applyGestures (b, g, role);
                    const juce::String what = juce::String (gestureProfileNames[prof]) + " / " + styleNames[st] + " / role " + juce::String ((int) role);

                    expectEquals ((int) a.notes.size(), (int) b.notes.size(), what);
                    const float maxSemis = std::max (gestureDepthSemitones (g.depth), role == GestureRole::bass ? 12.0f : 0.0f);
                    for (size_t i = 0; i < a.notes.size(); ++i)
                    {
                        const auto& n = a.notes[i];
                        expect (n.gesture.size() == b.notes[i].gesture.size(), what + " not deterministic");
                        expect (n.start >= 0.0 && n.end() <= a.lengthBeats + 1.0e-6, what + ": note outside the loop");
                        const float glide = n.glideFrom >= 0 ? (float) std::abs (n.glideFrom - n.pitch) : 0.0f;
                        double t = -1.0;
                        for (const auto& pt : n.gesture)
                        {
                            expect (pt.t >= t - 1.0e-9, what + ": gesture curve out of order");
                            t = pt.t;
                            expectLessOrEqual (pt.t, n.length + 1.0e-6, what + ": gesture past the note");
                            expectLessOrEqual (std::abs (pt.v), std::max (maxSemis, glide) + 1.0e-3f,
                                               what + ": gesture deeper than Depth (" + gestureNames[n.gestureKind] + ")");
                        }
                    }

                    ExprParams ep;
                    shapeExpression (a, ep);
                    juce::String why;
                    expect (channelsAreExclusive (renderMpe (a), why), what + ": " + why);
                    for (const auto& n : a.notes)
                        for (const auto& pt : n.bend)
                            expectLessOrEqual (std::abs (pt.v), 48.0f, what + ": bend beyond the MPE range");
                }

        beginTest ("Bend Riff: a run of 16ths becomes one note that bends through the run's pitches");
        {
            Phrase p;
            p.lengthBeats = 4.0;
            const int pitches[] = { 60, 62, 63, 62 };
            for (int i = 0; i < 4; ++i)
            {
                Note n;
                n.start = i * 0.25;
                n.length = 0.22;
                n.pitch = pitches[i];
                n.exprSeed = 31;
                p.notes.push_back (n);
            }
            GestureParams g;
            g.profile = GestureProfile::liquid; // riff weight 1: the run always merges
            g.riff = 1.0f;
            g.amount = 0.0f;
            g.depth = 0.5f;
            applyGestures (p, g, GestureRole::lead);
            expectLessThan ((int) p.notes.size(), 4, "the run must merge");
            const auto& riff = p.notes.front();
            expectEquals (riff.gestureKind, (int) Gesture::riff);
            expectEquals (riff.vibrato, 0.0f);
            const int merged = (int) std::lround (riff.end() / 0.25);
            expectGreaterOrEqual (merged, 2);
            for (int k = 1; k < merged; ++k)
                expectWithinAbsoluteError (evalCurve (riff.gesture, k * 0.25 + 0.08, 0.0f), (float) (pitches[k] - 60), 1.0e-3f,
                                           "the riff must reach every pitch of the run on its beat");
            expect (! riff.gesturePress.empty() && ! riff.gestureTimbre.empty(), "the attacks are re-articulated");

            // Mono render: one note-on for the whole riff, the pitches are bends.
            shapeExpression (p, {});
            int noteOns = 0;
            for (auto* ev : renderMono (p, 12, false))
                noteOns += ev->message.isNoteOn() ? 1 : 0;
            expectEquals (noteOns, (int) p.notes.size());
        }

        beginTest ("Profiles have their own vocabulary");
        {
            auto run = [] (GestureProfile prof, GestureRole role)
            {
                auto p = longNotes (32, 0.6); // with room after every note (falls and dives don't lean into the next)
                GestureParams g;
                g.profile = prof;
                g.amount = 1.0f;
                g.depth = 0.6f;
                applyGestures (p, g, role);
                return kinds (p);
            };
            expect (run (GestureProfile::classic, GestureRole::lead) == std::set<int> { 0 });
            const auto glitch = run (GestureProfile::glitch, GestureRole::lead);
            expect (glitch.count ((int) Gesture::trill) != 0, "Glitch trills");
            const auto vocal = run (GestureProfile::vocal, GestureRole::lead);
            expect (vocal.count ((int) Gesture::fall) != 0 || vocal.count ((int) Gesture::scoop) != 0, "Vocal scoops and falls");
            const auto bass = run (GestureProfile::aggressive, GestureRole::bass);
            expect (bass.count ((int) Gesture::dive) != 0, "an aggressive bass dives");
            expect (bass.count ((int) Gesture::trill) == 0 && bass.count ((int) Gesture::lift) == 0, "a bass never trills or lifts");

            // A dip goes down a scale step (1 or 2 semitones) and comes back.
            auto p = longNotes (32, 0.9);
            GestureParams g;
            g.profile = GestureProfile::liquid;
            g.amount = 1.0f;
            g.depth = 0.2f;
            applyGestures (p, g, GestureRole::lead);
            int dips = 0;
            for (const auto& n : p.notes)
                if (n.gestureKind == (int) Gesture::dip)
                {
                    ++dips;
                    float lo = 0.0f;
                    for (const auto& pt : n.gesture)
                        lo = std::min (lo, pt.v);
                    expect (lo <= -1.0f + 1.0e-3f && lo >= -2.0f - 1.0e-3f, "a dip is one scale step");
                    expectWithinAbsoluteError (n.gesture.back().v, 0.0f, 1.0e-4f, "a dip comes back");
                }
            expectGreaterThan (dips, 0);
        }

        beginTest ("Phrase form: a repeated A repeats its gestures");
        for (int st = 0; st < (int) Style::count; ++st)
        {
            GenParams gp;
            gp.form = Form::abac;
            gp.style = (Style) st;
            gp.seed = 500 + st;
            auto mel = generateMelody (gp);
            GestureParams g;
            g.profile = GestureProfile::glitch;
            g.amount = 1.0f;
            g.riff = 0.7f;
            g.seed = gp.seed;
            applyGestures (mel, g, GestureRole::lead);
            auto bar = [&mel] (int b)
            {
                std::vector<std::pair<long, int>> v;
                for (size_t i = 1; i + 1 < mel.notes.size(); ++i)
                {
                    const auto& n = mel.notes[i];
                    // The first note of a section may glide in from what came before and the last may lean into
                    // what follows: both differ between the two A's.
                    const int own = (int) std::floor (n.start / 4.0);
                    // A glide into the voice starts from the pedal, which follows the chord: skip those too.
                    if (n.pedal || n.glideFrom >= 0 || own != b || (int) std::floor (mel.notes[i - 1].start / 4.0) != b
                        || (int) std::floor (mel.notes[i + 1].start / 4.0) != b)
                        continue;
                    v.push_back ({ std::lround ((n.start - b * 4.0) * 1000.0), n.gestureKind * 1000 + (int) n.gesture.size() });
                }
                return v;
            };
            expect (bar (0) == bar (2), juce::String (styleNames[st]) + ": A must repeat its gestures");
        }

        beginTest ("Chords: every voice of a stab does the same gesture");
        {
            KitParams kp;
            kp.layers[(size_t) Layer::stab].pattern = (int) StabPattern::offbeat;
            Phrase stab;
            for (auto& part : generateKit (kp))
                if (part.layer == Layer::stab)
                    stab = part.phrase;
            GestureParams g;
            g.profile = GestureProfile::aggressive;
            g.amount = 1.0f;
            applyGestures (stab, g, GestureRole::chord);
            int gestured = 0;
            for (size_t i = 1; i < stab.notes.size(); ++i)
                if (std::abs (stab.notes[i].start - stab.notes[i - 1].start) < 1.0e-6)
                    expectEquals (stab.notes[i].gestureKind, stab.notes[i - 1].gestureKind);
            for (const auto& n : stab.notes)
                gestured += n.gestureKind != 0 ? 1 : 0;
            expectGreaterThan (gestured, 0);
            shapeExpression (stab, {});
            juce::String why;
            expect (channelsAreExclusive (renderMpe (stab), why), why);
        }

        beginTest ("Event density with gestures stays reasonable");
        {
            GenParams gp;
            gp.bars = 8;
            for (int prof = 1; prof < (int) GestureProfile::count; ++prof)
            {
                auto lead = generateMelody (gp);
                GestureParams g;
                g.profile = (GestureProfile) prof;
                g.amount = 1.0f;
                g.depth = 1.0f;
                applyGestures (lead, g, GestureRole::lead);
                shapeExpression (lead, {});
                const int events = renderMpe (lead).getNumEvents();
                std::cout << "    " << gestureProfileNames[prof] << ": " << events << " events, 8 bars" << std::endl;
                expectLessThan (events, 9000, gestureProfileNames[prof]);
            }
        }
    }
};

static GestureTests gestureTests;

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

    // Phrase forms: the same seed as classic, as A B A C / period / sentence / sequence (8 bars).
    for (auto form : { Form::abac, Form::period, Form::sentence, Form::sequence, Form::aaab })
    {
        GenParams gp;
        gp.style = Style::pursuit;
        gp.seed = 666;
        gp.bars = 8;
        gp.form = form;
        save (generateMelody (gp), juce::String ("Form - Lead Pursuit - ") + formNames[(int) form]);
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
    runner.runTests ({ &engineTests, &mpeImportTests, &cinematicTests, &harmonyTests, &kitTests, &formTests, &renderTests, &expressionTests, &gestureTests, &chordTests });

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}

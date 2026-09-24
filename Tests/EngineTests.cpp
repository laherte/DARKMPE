#include <juce_audio_basics/juce_audio_basics.h>

#include "engine/CinematicEngine.h"
#include "engine/HarmonyEngine.h"
#include "engine/Humanize.h"
#include "engine/ExpressionShaper.h"
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

static EngineTests engineTests;
static CinematicTests cinematicTests;
static MpeImportTests mpeImportTests;

// DarkMPETests --render <inDir> <outDir>: writes demo MPE clips (every style + every voicing of the .mid files in inDir).
static int renderExamples (const juce::File& inDir, const juce::File& outDir)
{
    outDir.createDirectory();
    auto save = [&] (Phrase p, const juce::String& name)
    {
        shapeExpression (p, {});
        const auto f = outDir.getChildFile (juce::File::createLegalFileName (name) + ".mid");
        writeMidiFile (makeMidiFile (renderMpe (p), 125.0, name), f);
        std::cout << "wrote " << f.getFileName() << "  (" << p.notes.size() << " notes)" << std::endl;
    };

    for (int s = 0; s < (int) Style::count; ++s)
    {
        GenParams gp;
        gp.style = (Style) s;
        gp.seed = 666 + s;
        save (generateMelody (gp), juce::String ("Lead - ") + styleNames[s]);
    }

    auto cine = [&] (const Phrase& src, const juce::String& base)
    {
        for (int m = 0; m < (int) Motion::count; ++m)
            for (int r = 0; r < (int) Reharm::count; ++r)
            {
                CineParams cp;
                cp.motion = (Motion) m;
                cp.reharm = (Reharm) r;
                cp.shape = m == 0 ? GlideShape::linear : GlideShape::swoopOut;
                save (cinematic (src, cp), "Cinematic - " + base + " - " + motionNames[m] + " - " + reharmNames[r]);
            }
    };
    cine (demoProgression (9, scales::Scale::naturalMinor, 4), "Demo Am");

    for (const auto& f : inDir.findChildFiles (juce::File::findFiles, false, "*.mid"))
    {
        Phrase src;
        juce::String err;
        if (! loadMidiFile (f, src, err))
            continue;
        if (! isMonophonic (src))
            cine (src, f.getFileNameWithoutExtension());
        for (int m = 0; m < (int) VoicingMode::count; ++m)
        {
            VoicingParams vp;
            vp.mode = (VoicingMode) m;
            vp.strum = 0.02;
            save (applyVoicing (src, vp), f.getFileNameWithoutExtension() + " - " + voicingNames[m]);
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
    runner.runTests ({ &engineTests, &mpeImportTests, &cinematicTests, &harmonyTests, &renderTests });

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}

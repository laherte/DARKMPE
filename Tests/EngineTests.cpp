#include <juce_audio_basics/juce_audio_basics.h>

#include "engine/CinematicEngine.h"
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
    runner.runTests ({ &engineTests, &mpeImportTests, &cinematicTests, &renderTests });

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}

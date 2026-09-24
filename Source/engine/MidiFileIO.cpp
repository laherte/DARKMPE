#include "MidiFileIO.h"

#include <array>
#include <cmath>

namespace dmpe
{

namespace
{

void addPoint (Curve& c, double t, float v)
{
    t = std::max (0.0, t);
    if (! c.empty() && t - c.back().t < 1.0e-6)
        c.back().v = v;
    else
        c.push_back ({ t, v });
}

// A curve that never moves off its neutral value carries no expression: drop it so the shaper can fill it.
void dropIfNeutral (Curve& c, float neutral, float tolerance)
{
    for (const auto& p : c)
        if (std::abs (p.v - neutral) > tolerance)
            return;
    c.clear();
}

struct ChannelState
{
    float bend = 0.0f;   // semitones
    bool bendSeen = false;
    float slide = -1.0f; // -1 = never sent
    float pressure = -1.0f;
    int rpnMsb = 127, rpnLsb = 127;
    int range = 48;
    std::vector<size_t> sounding; // indices into notes
};

bool isMcm (const juce::MidiMessageSequence& seq)
{
    int msb[17] {}, lsb[17] {};
    for (auto& v : msb) v = 127;
    for (auto& v : lsb) v = 127;
    for (auto* ev : seq)
    {
        const auto& m = ev->message;
        if (! m.isController())
            continue;
        const int ch = m.getChannel();
        const int cc = m.getControllerNumber();
        if (cc == 101) msb[ch] = m.getControllerValue();
        else if (cc == 100) lsb[ch] = m.getControllerValue();
        else if (cc == 6 && msb[ch] == 0 && lsb[ch] == 6 && (ch == 1 || ch == 16))
            return true;
    }
    return false;
}

} // namespace

Phrase phraseFromBeatSequence (const juce::MidiMessageSequence& source, LoadInfo* info)
{
    // ---- pre-scan: is this MPE?
    std::array<int, 17> notesPerChannel {};
    bool memberBends = false;
    for (auto* ev : source)
    {
        const auto& m = ev->message;
        if (m.isNoteOn())
            ++notesPerChannel[(size_t) m.getChannel()];
        else if (m.isPitchWheel() && m.getChannel() != 1 && m.getPitchWheelValue() != 8192)
            memberBends = true;
    }
    int channelsUsed = 0;
    for (int ch = 1; ch <= 16; ++ch)
        channelsUsed += notesPerChannel[(size_t) ch] > 0 ? 1 : 0;

    const bool mpe = isMcm (source) || channelsUsed >= 3 || (channelsUsed >= 2 && memberBends);
    const int defaultRange = mpe ? 48 : 2;

    std::array<ChannelState, 17> chans;
    for (int ch = 1; ch <= 16; ++ch)
        chans[(size_t) ch].range = (mpe && ch == 1) ? 2 : defaultRange; // lower-zone master uses +-2

    std::vector<Note> notes;
    std::vector<bool> closed;

    auto closeNote = [&] (int ch, int pitch, double t)
    {
        auto& st = chans[(size_t) ch];
        for (auto it = st.sounding.begin(); it != st.sounding.end(); ++it)
            if (notes[*it].pitch == pitch) // oldest first: FIFO pairing per channel
            {
                auto& n = notes[*it];
                n.length = std::max (1.0 / 64.0, t - n.start);
                closed[*it] = true;
                st.sounding.erase (it);
                return;
            }
    };

    for (auto* ev : source)
    {
        const auto& m = ev->message;
        const double t = m.getTimeStamp();
        const int ch = m.getChannel();
        if (ch < 1 || ch > 16)
            continue;
        auto& st = chans[(size_t) ch];

        if (m.isNoteOn())
        {
            Note n;
            n.start = t;
            n.pitch = m.getNoteNumber();
            n.velocity = m.getFloatVelocity();
            if (st.bendSeen)          addPoint (n.bend, 0.0, st.bend);
            if (st.slide >= 0.0f)     addPoint (n.slide, 0.0, st.slide);
            if (st.pressure >= 0.0f)  addPoint (n.pressure, 0.0, st.pressure);
            st.sounding.push_back (notes.size());
            notes.push_back (std::move (n));
            closed.push_back (false);
        }
        else if (m.isNoteOff())
        {
            closeNote (ch, m.getNoteNumber(), t);

            // MPE senders set bend/timbre/pressure right before each note-on; values left over from a
            // finished note must not leak into the next note on this channel.
            if (mpe && ch != 1 && st.sounding.empty())
            {
                st.bend = 0.0f;
                st.bendSeen = false;
                st.slide = -1.0f;
                st.pressure = -1.0f;
            }
        }
        else if (m.isPitchWheel())
        {
            st.bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * (float) st.range;
            st.bendSeen = true;
            for (auto idx : st.sounding)
                addPoint (notes[idx].bend, t - notes[idx].start, st.bend);
        }
        else if (m.isChannelPressure())
        {
            st.pressure = (float) m.getChannelPressureValue() / 127.0f;
            for (auto idx : st.sounding)
                addPoint (notes[idx].pressure, t - notes[idx].start, st.pressure);
        }
        else if (m.isAftertouch())
        {
            for (auto idx : st.sounding)
                if (notes[idx].pitch == m.getNoteNumber())
                    addPoint (notes[idx].pressure, t - notes[idx].start, (float) m.getAfterTouchValue() / 127.0f);
        }
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            const int v = m.getControllerValue();
            if (cc == 74)
            {
                st.slide = (float) v / 127.0f;
                for (auto idx : st.sounding)
                    addPoint (notes[idx].slide, t - notes[idx].start, st.slide);
            }
            else if (cc == 101) st.rpnMsb = v;
            else if (cc == 100) st.rpnLsb = v;
            else if (cc == 6 && st.rpnMsb == 0 && st.rpnLsb == 0 && v > 0)
                st.range = v; // RPN 0: pitch-bend sensitivity
            else if (cc == 123 || cc == 120)
            {
                for (auto idx : st.sounding)
                {
                    notes[idx].length = std::max (1.0 / 64.0, t - notes[idx].start);
                    closed[idx] = true;
                }
                st.sounding.clear();
            }
        }
    }

    Phrase p;
    double lastEnd = 0.0;
    LoadInfo li;
    li.mpe = mpe;
    li.channels = channelsUsed;
    li.bendRange = defaultRange;

    for (size_t i = 0; i < notes.size(); ++i)
    {
        auto& n = notes[i];
        if (! closed[i])
            n.length = 0.25; // hanging note: give it a sensible length

        // Clip curves to the note and drop the ones that never move.
        for (auto* c : { &n.bend, &n.slide, &n.pressure })
            while (! c->empty() && c->back().t > n.length + 1.0e-6)
                c->pop_back();
        dropIfNeutral (n.bend, 0.0f, 0.01f);
        dropIfNeutral (n.pressure, 0.0f, 0.004f);
        dropIfNeutral (n.slide, 64.0f / 127.0f, 0.004f);

        n.hasSourceExpr = ! (n.bend.empty() && n.slide.empty() && n.pressure.empty());
        li.notesWithExpression += n.hasSourceExpr ? 1 : 0;
        lastEnd = std::max (lastEnd, n.end());
        p.notes.push_back (std::move (n));
    }

    li.notes = (int) p.notes.size();
    if (info != nullptr)
        *info = li;

    p.lengthBeats = std::max (4.0, std::ceil (lastEnd / 4.0 - 1.0e-6) * 4.0);
    p.sortByStart();
    return p;
}

bool loadMidiStream (juce::InputStream& in, Phrase& out, juce::String& error, LoadInfo* info)
{
    juce::MidiFile mf;
    if (! mf.readFrom (in))
    {
        error = "Not a valid MIDI file";
        return false;
    }

    const int tf = mf.getTimeFormat();
    if (tf <= 0)
    {
        error = "SMPTE-timed MIDI files are not supported";
        return false;
    }

    // Merge all tracks, keeping each event's channel and the file order of simultaneous events.
    juce::MidiMessageSequence merged;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (auto* ev : *mf.getTrack (t))
        {
            const auto& m = ev->message;
            if (m.isMetaEvent() || m.isSysEx())
                continue;
            auto copy = m;
            copy.setTimeStamp (m.getTimeStamp() / (double) tf);
            merged.addEvent (copy);
        }

    out = phraseFromBeatSequence (merged, info);
    if (out.empty())
    {
        error = "The MIDI file contains no notes";
        return false;
    }
    return true;
}

bool loadMidiFile (const juce::File& file, Phrase& out, juce::String& error, LoadInfo* info)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
    {
        error = "Cannot open " + file.getFileName();
        return false;
    }
    return loadMidiStream (in, out, error, info);
}

juce::MidiFile makeMidiFile (const juce::MidiMessageSequence& beatSeq, double bpm, const juce::String& trackName)
{
    juce::MidiMessageSequence meta;
    meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    meta.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / std::max (1.0, bpm))), 0.0);
    meta.addEvent (juce::MidiMessage::endOfTrack(), 0.0);

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, trackName), 0.0);
    double last = 0.0;
    for (auto* ev : beatSeq)
    {
        auto m = ev->message;
        const double t = m.getTimeStamp();
        last = std::max (last, t);
        m.setTimeStamp (std::round (t * filePpq));
        track.addEvent (m);
    }
    track.addEvent (juce::MidiMessage::endOfTrack(), std::round (last * filePpq));

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (filePpq);
    mf.addTrack (meta);
    mf.addTrack (track);
    return mf;
}

bool writeMidiFile (const juce::MidiFile& midi, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return false;
    return midi.writeTo (out, 1);
}

} // namespace dmpe

// Runs the real DarkMPEProcessor offline and checks that what it sends out is MPE:
// one member channel per sounding note, per-channel pitch bend, zone configuration, no hanging notes.

#include "PluginProcessor.h"

#include <chrono>
#include <iostream>
#include <map>
#include <set>

// ---- heap allocation counter (glibc only): the audio thread must never allocate.
#if defined(__GLIBC__)
extern "C" void* __libc_malloc (size_t);
extern "C" void* __libc_calloc (size_t, size_t);
extern "C" void* __libc_realloc (void*, size_t);
extern "C" void __libc_free (void*);

namespace
{
thread_local bool countAllocations = false;
std::atomic<long> allocations { 0 };
void noteAllocation() { if (countAllocations) allocations.fetch_add (1, std::memory_order_relaxed); }
} // namespace

extern "C" void* malloc (size_t n) noexcept { noteAllocation(); return __libc_malloc (n); }
extern "C" void* calloc (size_t n, size_t s) noexcept { noteAllocation(); return __libc_calloc (n, s); }
extern "C" void* realloc (void* p, size_t n) noexcept { noteAllocation(); return __libc_realloc (p, n); }
extern "C" void free (void* p) noexcept { __libc_free (p); }
 #define DARKMPE_COUNTS_ALLOCATIONS 1
#else
 #define DARKMPE_COUNTS_ALLOCATIONS 0
#endif

namespace
{

struct Captured
{
    long long sample;
    juce::MidiMessage msg;
};

void setParam (DarkMPEProcessor& p, const char* id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

std::vector<Captured> run (DarkMPEProcessor& proc, int blocks, int blockSize, long long& clock)
{
    std::vector<Captured> out;
    juce::AudioBuffer<float> audio (2, blockSize);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        proc.processBlock (audio, midi);
        for (const auto meta : midi)
            out.push_back ({ clock + meta.samplePosition, meta.getMessage() });
        clock += blockSize;
    }
    return out;
}

// Like run(), feeding `input` (block index -> messages at sample 0) into the plugin's MIDI input.
std::vector<Captured> runWithInput (DarkMPEProcessor& proc, int blocks, int blockSize, long long& clock,
                                    const std::map<int, std::vector<juce::MidiMessage>>& input)
{
    std::vector<Captured> out;
    juce::AudioBuffer<float> audio (2, blockSize);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (auto it = input.find (b); it != input.end())
            for (const auto& m : it->second)
                midi.addEvent (m, 0);
        proc.processBlock (audio, midi);
        for (const auto meta : midi)
            out.push_back ({ clock + meta.samplePosition, meta.getMessage() });
        clock += blockSize;
    }
    return out;
}

int hangingNotes (const std::vector<Captured>& events)
{
    std::set<std::pair<int, int>> open;
    for (const auto& e : events)
    {
        if (e.msg.isNoteOn()) open.insert ({ e.msg.getChannel(), e.msg.getNoteNumber() });
        else if (e.msg.isNoteOff()) open.erase ({ e.msg.getChannel(), e.msg.getNoteNumber() });
    }
    return (int) open.size();
}

std::vector<int> notePitches (const std::vector<Captured>& events)
{
    std::vector<int> v;
    for (const auto& e : events)
        if (e.msg.isNoteOn())
            v.push_back (e.msg.getNoteNumber());
    return v;
}

// Plays `blocks` blocks into a host buffer that is reused (like a real host) and returns
// {heap allocations inside processBlock, average microseconds per block}.
std::pair<long, double> measure (DarkMPEProcessor& proc, int blocks, int blockSize)
{
    juce::AudioBuffer<float> audio (2, blockSize);
    juce::MidiBuffer midi;
    midi.ensureSize (65536);
    double total = 0.0;
    long allocs = 0;
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
       #if DARKMPE_COUNTS_ALLOCATIONS
        const long before = allocations.load();
        countAllocations = true;
       #endif
        const auto t0 = std::chrono::steady_clock::now();
        proc.processBlock (audio, midi);
        total += std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
       #if DARKMPE_COUNTS_ALLOCATIONS
        countAllocations = false;
        if (b > 0) // the very first block may lazily set things up
            allocs += allocations.load() - before;
       #endif
    }
    return { allocs, total / blocks };
}

} // namespace

class ProcessorTests : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest ("DarkMPE processor output") {}

    void checkMpe (const std::vector<Captured>& events, const juce::String& what, int minBendingChannels)
    {
        // Zone configuration (MCM: RPN 6 on channel 1) at playback start.
        bool mcm = false;
        int rpnMsb = -1, rpnLsb = -1;
        for (const auto& e : events)
            if (e.msg.isController() && e.msg.getChannel() == 1)
            {
                if (e.msg.getControllerNumber() == 101) rpnMsb = e.msg.getControllerValue();
                if (e.msg.getControllerNumber() == 100) rpnLsb = e.msg.getControllerValue();
                if (e.msg.getControllerNumber() == 6 && rpnMsb == 0 && rpnLsb == 6 && e.msg.getControllerValue() == 15)
                    mcm = true;
            }
        expect (mcm, what + ": MPE zone configuration (MCM) not sent");

        // One note per member channel while sounding.
        std::map<int, int> sounding;
        int notes = 0;
        std::set<int> channelsUsed;
        for (const auto& e : events)
        {
            const auto& m = e.msg;
            if (m.isNoteOn())
            {
                ++notes;
                expect (m.getChannel() >= 2 && m.getChannel() <= 16, what + ": note on master channel");
                expect (sounding.count (m.getChannel()) == 0,
                        what + ": two notes share channel " + juce::String (m.getChannel()));
                sounding[m.getChannel()] = m.getNoteNumber();
                channelsUsed.insert (m.getChannel());
            }
            else if (m.isNoteOff())
                sounding.erase (m.getChannel());
        }
        expectGreaterThan (notes, 3, what + ": no notes played");
        expectGreaterThan ((int) channelsUsed.size(), 2, what + ": notes must spread over member channels");

        // Different channels bending to different values at the same moment = per-note pitch.
        std::map<int, int> lastBend;
        int maxDistinct = 0;
        for (const auto& e : events)
            if (e.msg.isPitchWheel() && e.msg.getChannel() >= 2)
            {
                lastBend[e.msg.getChannel()] = e.msg.getPitchWheelValue();
                std::set<int> distinct;
                for (auto [ch, v] : lastBend)
                    if (v != 8192)
                        distinct.insert (v);
                maxDistinct = std::max (maxDistinct, (int) distinct.size());
            }
        expectGreaterOrEqual (maxDistinct, minBendingChannels, what + ": pitch bend is not per-channel");
    }

    void runTest() override
    {
        const int blockSize = 512;
        const int blocksFor8Bars = (int) (16.0 * 48000.0 / blockSize) + 1; // 8 bars at 120 bpm

        beginTest ("Cinematic preview outputs real MPE");
        {
            DarkMPEProcessor proc;
            setParam (proc, "virtualOut", 0.0f);
            setParam (proc, "mode", 2.0f);    // Cinematic (demo progression)
            setParam (proc, "cMotion", 0.0f); // Morph
            setParam (proc, "preview", 1.0f);
            proc.refreshNow();
            proc.prepareToPlay (48000.0, blockSize);

            long long clock = 0;
            auto events = run (proc, blocksFor8Bars, blockSize, clock);
            checkMpe (events, "cinematic", 3);

            // Stop: everything must be released.
            setParam (proc, "preview", 0.0f);
            auto tail = run (proc, 2, blockSize, clock);
            events.insert (events.end(), tail.begin(), tail.end());
            std::set<std::pair<int, int>> open;
            for (const auto& e : events)
            {
                if (e.msg.isNoteOn()) open.insert ({ e.msg.getChannel(), e.msg.getNoteNumber() });
                else if (e.msg.isNoteOff()) open.erase ({ e.msg.getChannel(), e.msg.getNoteNumber() });
            }
            expect (open.empty(), "hanging notes after stop: " + juce::String ((int) open.size()));

            // The monitor saw per-channel activity.
            int monitored = 0;
            for (int ch = 2; ch <= 16; ++ch)
                monitored += std::abs (proc.monitor (ch).bend.load()) > 0.01f ? 1 : 0;
            expectGreaterThan (monitored, 1, "MPE monitor did not record channel bends");
        }

        beginTest ("Transform of a polyphonic file outputs real MPE");
        {
            DarkMPEProcessor proc;
            setParam (proc, "virtualOut", 0.0f);
            juce::String err;
            const auto file = juce::File (DARKMPE_SOURCE_DIR).getChildFile ("Examples/Dark Chords Am.mid");
            expect (proc.loadMidi (file, err), err);
            setParam (proc, "mode", 1.0f);  // Transform
            setParam (proc, "vMode", 2.0f); // Open Spread
            setParam (proc, "preview", 1.0f);
            proc.refreshNow();
            proc.prepareToPlay (48000.0, blockSize);
            long long clock = 0;
            checkMpe (run (proc, blocksFor8Bars, blockSize, clock), "transform", 3);
        }

        beginTest ("Generated lead outputs MPE with glides on member channels");
        {
            DarkMPEProcessor proc;
            setParam (proc, "virtualOut", 0.0f);
            setParam (proc, "mode", 0.0f);
            setParam (proc, "slide", 1.0f);
            setParam (proc, "preview", 1.0f);
            proc.refreshNow();
            proc.prepareToPlay (48000.0, blockSize);
            long long clock = 0;
            checkMpe (run (proc, blocksFor8Bars, blockSize, clock), "generate", 1);
        }

        beginTest ("Mono lead: channel 1 with its bend range, no MPE zone");
        {
            DarkMPEProcessor proc;
            setParam (proc, "virtualOut", 0.0f);
            setParam (proc, "mode", 0.0f);
            setParam (proc, "leadMono", 1.0f);
            setParam (proc, "monoBend", 24.0f);
            setParam (proc, "slide", 1.0f);
            setParam (proc, "preview", 1.0f);
            proc.refreshNow();
            proc.prepareToPlay (48000.0, blockSize);
            long long clock = 0;
            const auto events = run (proc, blocksFor8Bars, blockSize, clock);
            int notes = 0, bends = 0;
            bool range = false;
            for (const auto& e : events)
            {
                expect (! (e.msg.isController() && e.msg.getControllerNumber() == 100 && e.msg.getControllerValue() == 6),
                        "mono output must not send the MPE zone message (MCM)");
                if (e.msg.isNoteOn()) { ++notes; expectEquals (e.msg.getChannel(), 1); }
                if (e.msg.isPitchWheel() && e.msg.getPitchWheelValue() != 8192) ++bends;
                if (e.msg.isController() && e.msg.getControllerNumber() == 6 && e.msg.getControllerValue() == 24) range = true;
            }
            expectGreaterThan (notes, 10);
            expectGreaterThan (bends, 10);
            expect (range, "mono bend range (RPN 0) not sent");
            expect (proc.isHostMono());
        }

        beginTest ("Key Trigger: transpose follows the played key, gate plays only while held");
        {
            auto makeLead = [] (DarkMPEProcessor& p, int trig)
            {
                setParam (p, "virtualOut", 0.0f);
                setParam (p, "mode", 0.0f);
                setParam (p, "key", 9.0f); // A
                setParam (p, "trigMode", (float) trig);
                p.setSeed (4242, 0);
                p.refreshNow();
                p.prepareToPlay (48000.0, blockSize);
            };

            // Transpose: C (3 semitones above A) shifts every note by +3 against the same seed untransposed.
            DarkMPEProcessor plain, shifted;
            makeLead (plain, 0);
            makeLead (shifted, 1);
            setParam (plain, "preview", 1.0f);
            setParam (shifted, "preview", 1.0f);
            long long c1 = 0, c2 = 0;
            const auto a = notePitches (run (plain, 200, blockSize, c1));
            const auto b = notePitches (runWithInput (shifted, 200, blockSize, c2, { { 0, { juce::MidiMessage::noteOn (1, 60, 0.8f) } } }));
            expectEquals ((int) a.size(), (int) b.size());
            bool plus3 = ! a.empty();
            for (size_t i = 0; i < a.size() && i < b.size(); ++i)
                plus3 = plus3 && b[i] == a[i] + 3;
            expect (plus3, "transpose must shift every note by +3");
            expectEquals (shifted.getTranspose(), 3);

            // F (4 semitones below A) folds to -4.
            long long c3 = 0;
            runWithInput (shifted, 2, blockSize, c3, { { 0, { juce::MidiMessage::noteOn (1, 65, 0.8f) } } });
            expectEquals (shifted.getTranspose(), -4);

            // Gate: nothing without keys (transport stopped, no preview), notes while held, all released after.
            DarkMPEProcessor gate;
            makeLead (gate, 2);
            long long c4 = 0;
            const auto silent = run (gate, 50, blockSize, c4);
            expectEquals ((int) notePitches (silent).size(), 0, "gate must be silent without keys");
            auto events = runWithInput (gate, 200, blockSize, c4, { { 0, { juce::MidiMessage::noteOn (1, 57, 0.8f) } },
                                                                    { 150, { juce::MidiMessage::noteOff (1, 57) } } });
            const auto tail = run (gate, 5, blockSize, c4);
            events.insert (events.end(), tail.begin(), tail.end());
            expectGreaterThan ((int) notePitches (events).size(), 5, "gate must play while a key is held");
            expectEquals (hangingNotes (events), 0, "gate release left notes on");
            int afterRelease = 0;
            const long long released = (50 + 150 + 1) * (long long) blockSize; // after the silent 50 blocks
            for (const auto& e : events)
                afterRelease += (e.msg.isNoteOn() && e.sample >= released) ? 1 : 0;
            expectEquals (afterRelease, 0, "notes after the key was released");

            // Changing the transpose while long cinematic notes sound never leaves notes on.
            DarkMPEProcessor cine;
            setParam (cine, "virtualOut", 0.0f);
            setParam (cine, "mode", 2.0f);
            setParam (cine, "trigMode", 1.0f);
            setParam (cine, "preview", 1.0f);
            cine.refreshNow();
            cine.prepareToPlay (48000.0, blockSize);
            long long c5 = 0;
            auto cineEvents = runWithInput (cine, 300, blockSize, c5, { { 0, { juce::MidiMessage::noteOn (1, 60, 0.8f) } },
                                                                        { 120, { juce::MidiMessage::noteOn (1, 62, 0.8f) } },
                                                                        { 200, { juce::MidiMessage::noteOff (1, 62), juce::MidiMessage::noteOff (1, 60) } } });
            setParam (cine, "preview", 0.0f);
            const auto stop = run (cine, 3, blockSize, c5);
            cineEvents.insert (cineEvents.end(), stop.begin(), stop.end());
            expectEquals (hangingNotes (cineEvents), 0, "transpose change left notes on");
        }

        beginTest ("KIT: a stream per layer, host out follows the focus, export writes every layer");
        {
            DarkMPEProcessor proc;
            setParam (proc, "virtualOut", 0.0f);
            setParam (proc, "mode", 3.0f);
            setParam (proc, "kSiren", 1.0f);
            setParam (proc, "kFocus", 5.0f); // Pad
            setParam (proc, "preview", 1.0f);
            proc.refreshNow();
            auto r = proc.getRendered();
            expectEquals ((int) r->streams.size(), 6);
            expectEquals (r->focused().layer, 5);
            proc.prepareToPlay (48000.0, blockSize);
            long long clock = 0;
            auto events = run (proc, blocksFor8Bars / 2, blockSize, clock);
            checkMpe (events, "kit pad", 3);

            // Bass is mono by default: with the focus on it the host gets channel 1.
            setParam (proc, "kFocus", 1.0f);
            proc.refreshNow();
            const auto bass = run (proc, blocksFor8Bars / 2, blockSize, clock);
            int bassNotes = 0;
            for (const auto& e : bass)
                if (e.msg.isNoteOn())
                {
                    ++bassNotes;
                    expectEquals (e.msg.getChannel(), 1, "mono bass must be on channel 1");
                }
            expectGreaterThan (bassNotes, 8);
            events.insert (events.end(), bass.begin(), bass.end());
            setParam (proc, "preview", 0.0f);
            const auto stop = run (proc, 2, blockSize, clock);
            events.insert (events.end(), stop.begin(), stop.end());
            expectEquals (hangingNotes (events), 0, "switching the focus left notes on");

            const auto dir = juce::File::createTempFile ("dmpe-kit");
            dir.createDirectory();
            const auto files = proc.writeAllStreams (dir.getChildFile ("Kit.mid"));
            expectEquals (files.size(), 6);
            for (const auto& f : files)
            {
                dmpe::Phrase p;
                juce::String err;
                expect (dmpe::loadMidiFile (f, p, err) && ! p.empty(), f.getFileName() + ": " + err);
            }
            dir.deleteRecursively();

            setParam (proc, "preview", 1.0f);
            proc.prepareToPlay (48000.0, 64);
            const auto [allocs, us] = measure (proc, 6000, 64);
            std::cout << "    kit (6 layers): " << juce::String (us, 2) << " us/block, allocations: " << allocs << std::endl;
           #if DARKMPE_COUNTS_ALLOCATIONS
            expectEquals ((int) allocs, 0, "kit processBlock allocated memory");
           #endif
        }

        beginTest ("Presets: every factory preset plays, programs, user presets round-trip");
        {
            DarkMPEProcessor proc;
            setParam (proc, "virtualOut", 0.0f);
            auto value = [&] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
            for (int i = 1; i < proc.getNumPrograms(); ++i)
            {
                proc.setCurrentProgram (i);
                proc.refreshNow();
                expectEquals (proc.getCurrentProgram(), i);
                expectEquals (proc.getPresetName(), proc.getProgramName (i));
                int notes = 0;
                for (const auto& st : proc.getRendered()->streams)
                    notes += (int) st.phrase.notes.size();
                // Transform presets fall back to the lead when nothing is loaded; everything must play.
                expectGreaterThan (notes, 0, proc.getProgramName (i) + " plays nothing");
                expectEquals (value ("virtualOut"), 0.0f, "presets must not touch output settings");
            }

            // A host re-selecting the current program must not undo the user's edits.
            proc.setCurrentProgram (2);
            setParam (proc, "density", 0.1f);
            proc.setCurrentProgram (2);
            expectWithinAbsoluteError (value ("density"), 0.1f, 1.0e-4f);

            // User preset: parameters and seed come back.
            const auto file = juce::File::createTempFile ("dmpreset");
            setParam (proc, "cTension", 0.77f);
            proc.setSeed (777, 2);
            expect (proc.saveUserPreset (file));
            setParam (proc, "cTension", 0.1f);
            proc.setSeed (1, 0);
            expect (proc.loadUserPreset (file));
            expectWithinAbsoluteError (value ("cTension"), 0.77f, 1.0e-4f);
            expectEquals (proc.getSeed(), 777);
            expectEquals (proc.getVariation(), 2);
            file.deleteFile();
        }

        beginTest ("Form: section letters for the roll, in every generating mode");
        {
            DarkMPEProcessor proc;
            setParam (proc, "virtualOut", 0.0f);
            setParam (proc, "form", 2.0f); // A B A C
            auto labels = [&]
            {
                proc.refreshNow();
                juce::String s;
                for (const auto& [beat, label] : proc.getRendered()->sections)
                    s << label << "@" << juce::String (beat, 0) << " ";
                return s.trim();
            };
            setParam (proc, "mode", 0.0f);
            expectEquals (labels(), juce::String ("A@0 B@4 A@8 C@12"));
            setParam (proc, "mode", 3.0f);
            expectEquals (labels(), juce::String ("A@0 B@4 A@8 C@12"));
            setParam (proc, "mode", 2.0f);
            setParam (proc, "bars", 3.0f); // 8 bars of 1-bar chords: two chords per section
            expectEquals (labels(), juce::String ("A@0 B@8 A@16 C@24"));
            setParam (proc, "form", 0.0f);
            expectEquals (labels(), juce::String());
        }

        beginTest ("Seed history and favourites survive the saved state");
        {
            auto notesOf = [] (DarkMPEProcessor& p)
            {
                std::vector<std::pair<double, int>> v;
                for (const auto& n : p.getRendered()->focused().phrase.notes)
                    v.push_back ({ n.start, n.pitch });
                return v;
            };

            DarkMPEProcessor proc;
            setParam (proc, "virtualOut", 0.0f);
            std::vector<int> seeds;
            std::vector<std::vector<std::pair<double, int>>> phrases;
            for (int i = 0; i < 3; ++i)
            {
                proc.generateNew();
                seeds.push_back (proc.getSeed());
                phrases.push_back (notesOf (proc));
            }
            proc.historyBack();
            expectEquals (proc.getSeed(), seeds[1]);
            expect (notesOf (proc) == phrases[1], "back must restore the same phrase");
            proc.historyBack();
            expectEquals (proc.getSeed(), seeds[0]);
            expect (notesOf (proc) == phrases[0]);
            proc.historyForward();
            expectEquals (proc.getSeed(), seeds[1]);
            proc.mutate(); // a new branch drops the forward part
            expect (! proc.canGoForward());
            expectEquals (proc.getVariation(), 1);
            proc.toggleFavourite();
            expect (proc.isFavourite());

            juce::MemoryBlock state;
            proc.getStateInformation (state);
            DarkMPEProcessor copy;
            copy.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (copy.getSeed(), proc.getSeed());
            expectEquals (copy.getVariation(), 1);
            expect (copy.isFavourite());
            expect (copy.canGoBack());
            expect (notesOf (copy) == notesOf (proc), "restored state must render the same phrase");
        }

        beginTest ("Audio thread: no heap allocations, cost per block");
        {
            for (int motion : { 0, 1 }) // Morph (long dense notes), Bloom (many short notes)
            {
                DarkMPEProcessor proc;
                setParam (proc, "virtualOut", 0.0f);
                setParam (proc, "mode", 2.0f);
                setParam (proc, "bars", 3.0f); // 8 bars
                setParam (proc, "cMotion", (float) motion);
                setParam (proc, "preview", 1.0f);
                proc.refreshNow();
                proc.prepareToPlay (48000.0, 64);
                const int blocks = (int) (32.0 * 48000.0 / 64.0); // 16 bars at 120 bpm: loops twice
                const auto [allocs, usPerBlock] = measure (proc, blocks, 64);
                std::cout << "    cinematic motion " << motion << ": " << juce::String (usPerBlock, 2)
                          << " us/block (64 samples), allocations in processBlock: " << allocs << std::endl;
               #if DARKMPE_COUNTS_ALLOCATIONS
                expectEquals ((int) allocs, 0, "processBlock allocated memory");
               #endif
            }
        }
    }
};

static ProcessorTests processorTests;

// DarkMPEProcessorTests --snapshot <dir>: PNGs of the editor in every mode (100% and 75%), to review the layout.
static int snapshots (const juce::File& dir)
{
    dir.createDirectory();
    const char* modeNames[] = { "Generate", "Transform", "Cinematic", "Kit" };
    for (int mode = 0; mode < 4; ++mode)
        for (float scale : { 1.0f, 0.75f })
        {
            DarkMPEProcessor proc;
            setParam (proc, "virtualOut", 0.0f);
            if (mode == 1)
            {
                juce::String err;
                proc.loadMidi (juce::File (DARKMPE_SOURCE_DIR).getChildFile ("Examples/Dark Chords Am.mid"), err);
            }
            setParam (proc, "mode", (float) mode);
            setParam (proc, "kSiren", 1.0f);
            setParam (proc, "kFocus", 1.0f);
            setParam (proc, "form", mode == 1 ? 0.0f : 2.0f); // A B A C
            setParam (proc, "preview", 1.0f);
            proc.refreshNow();
            proc.prepareToPlay (48000.0, 512);
            long long clock = 0;
            run (proc, 200, 512, clock); // the MPE monitor shows what is sounding

            proc.apvts.state.setProperty ("uiScale", scale, nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = dir.getChildFile (juce::String (modeNames[mode]) + " " + juce::String (juce::roundToInt (scale * 100)) + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat png;
            if (! out.openedOk() || ! png.writeImageToStream (image, out))
                return 1;
            std::cout << "wrote " << file.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;
        }
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc == 3 && juce::String (argv[1]) == "--snapshot")
        return snapshots (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTests ({ &processorTests });

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "ALL PROCESSOR TESTS PASSED" : "PROCESSOR TESTS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}

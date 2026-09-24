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
    const char* modeNames[] = { "Generate", "Transform", "Cinematic" };
    for (int mode = 0; mode < 3; ++mode)
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

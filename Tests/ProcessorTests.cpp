// Runs the real DarkMPEProcessor offline and checks that what it sends out is MPE:
// one member channel per sounding note, per-channel pitch bend, zone configuration, no hanging notes.

#include "PluginProcessor.h"

#include <iostream>
#include <map>
#include <set>

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
    }
};

static ProcessorTests processorTests;

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTests ({ &processorTests });

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "ALL PROCESSOR TESTS PASSED" : "PROCESSOR TESTS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}

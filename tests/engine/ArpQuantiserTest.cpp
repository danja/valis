// tests/engine/ArpQuantiserTest.cpp
//
// circuits/arp-quantiser.ttl (scripts/elements-demo/arp-quantiser.py): a sequence of all twelve pitch classes forced into
// a key and sent out as MIDI. Every note that leaves the plugin is in the chosen scale, for each scale and several roots;
// each note-off matches its note-on; the channel and the transpose are what the panel says; and the same notes arrive
// in a MIDI file when the circuit is rendered offline with a MIDI output, at the step length the sequence runs at.

#include "ExampleRig.h"

#include "valis/MidiScore.h"
#include "valis/OfflineRender.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

using valis::test::ExampleRig;

namespace {

struct Sent { bool on; int note; int channel; long long sample; };

/// The note events a circuit sends over `seconds`, by stream position.
std::vector<Sent> sent(ExampleRig& rig, double seconds)
{
    std::vector<Sent> events;
    std::vector<float> left(ExampleRig::kBlock), right(ExampleRig::kBlock);
    const int blocks = static_cast<int>(seconds * ExampleRig::kRate / ExampleRig::kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        rig.engine.process(nullptr, left.data(), right.data(), ExampleRig::kBlock);
        for (int i = 0; i < rig.engine.outputEventCount(); ++i)
        {
            const auto& e = rig.engine.outputEvent(i);
            events.push_back({e.noteOn, e.note, e.channel, static_cast<long long>(b) * ExampleRig::kBlock + e.sampleOffset});
        }
    }
    return events;
}

std::vector<Sent> ons(const std::vector<Sent>& all)
{
    std::vector<Sent> out;
    std::copy_if(all.begin(), all.end(), std::back_inserter(out), [](const Sent& e) { return e.on; });
    return out;
}

/// Pitch classes of a scale of the given intervals above a root.
std::set<int> scaleOf(const std::vector<int>& intervals, int root)
{
    std::set<int> s;
    for (const int i : intervals)
        s.insert((root + i) % 12);
    return s;
}

}  // namespace

int main()
{
    const std::vector<int> major{0, 2, 4, 5, 7, 9, 11}, minor{0, 2, 3, 5, 7, 8, 10}, pentatonic{0, 2, 4, 7, 9};

    struct Case { int scale; std::vector<int> intervals; };
    const std::vector<Case> cases{{4, major}, {5, minor}, {6, pentatonic}, {2, {0}}, {0, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}}};

    for (const auto& c : cases)
        for (const int root : {0, 2, 9})
        {
            ExampleRig rig("arp-quantiser.ttl");
            rig.setParam("scale", static_cast<float>(c.scale));
            rig.setParam("root", static_cast<float>(root));
            const auto all = sent(rig, 2.3);
            const auto notes = ons(all);
            assert(notes.size() >= 16);

            const auto allowed = scaleOf(c.intervals, root);
            std::set<int> heard;
            for (const auto& n : notes)
            {
                assert(n.note >= 0 && n.note <= 127);
                assert(allowed.count(n.note % 12) == 1);
                heard.insert(n.note % 12);
            }
            // Free passes all twelve; a real scale loses some of them, and keeps at least two
            if (c.scale == 0)
                assert(heard.size() == 12);
            else
                assert(heard.size() < 12 && heard.size() >= std::min<std::size_t>(2, allowed.size()));

            // each note-off ends the note that was on, on its channel, and none is left hanging at a quiet moment
            std::vector<Sent> held;
            for (const auto& e : all)
            {
                if (e.on)
                    held.push_back(e);
                else
                {
                    const auto it = std::find_if(held.begin(), held.end(), [&](const Sent& h) { return h.note == e.note && h.channel == e.channel; });
                    assert(it != held.end());
                    held.erase(it);
                }
            }
            assert(held.size() <= 1);
        }

    // the channel the panel says
    {
        ExampleRig rig("arp-quantiser.ttl");
        rig.setParam("midi_channel", 5.0f);
        for (const auto& e : sent(rig, 1.0))
            assert(e.channel == 5);
    }

    // transpose moves the whole sequence, and the notes sent with it
    {
        ExampleRig plain("arp-quantiser.ttl"), up("arp-quantiser.ttl");
        up.setParam("transpose", 12.0f);
        const auto a = ons(sent(plain, 1.5)), b = ons(sent(up, 1.5));
        assert(a.size() == b.size() && a.size() >= 8);
        for (std::size_t i = 0; i < a.size(); ++i)
            assert(b[i].note == a[i].note + 12);
    }

    // offline, the render writes the same notes to a file, one every step (a sixteenth, 6000 samples at 120 bpm)
    {
        ExampleRig rig("arp-quantiser.ttl");
        valis::OfflineRenderRequest request;
        request.circuitPath = std::string(VALIS_CIRCUITS_DIR) + "/arp-quantiser.ttl";
        request.seconds = 4.0;
        request.tempo = 120.0;
        request.rolling = true;
        request.midiOutPath = "arp-quantiser-test.mid";
        const auto result = valis::renderCircuitFile(rig.ontology, rig.registry, request);
        assert(result.ok);
        assert(result.nonFinite == 0 && result.peak > 0.05 && result.peak <= 1.0);
        assert(result.midiEventsOut == 64);

        std::vector<valis::TimedNote> back;
        std::string error;
        const bool read = valis::readMidiScore(request.midiOutPath, 120.0, 48000.0, back, error);
        assert(read);
        std::remove(request.midiOutPath.c_str());

        std::vector<valis::TimedNote> onsets;
        std::copy_if(back.begin(), back.end(), std::back_inserter(onsets), [](const valis::TimedNote& n) { return n.on; });
        assert(onsets.size() == 32);
        const auto cMajor = scaleOf(major, 0);
        for (std::size_t i = 0; i < onsets.size(); ++i)
        {
            assert(cMajor.count(onsets[i].note % 12) == 1);
            assert(std::llabs(onsets[i].sample - static_cast<long long>(i) * 6000) < 100);
        }
    }

    std::puts("ArpQuantiserTest passed");
    return 0;
}

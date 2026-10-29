// tests/dsp/StepSequencerTest.cpp
//
// val:StepSequencer. A sequencer is judged by whether the right step plays at the
// right musical position, whatever the buffer size, and by how it refuses a
// pattern it cannot read.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

struct Out { float gate, note, pitch, step; };

Out at(ElementTestFixture& rig, double ppq)
{
    rig.setTransport(true, 120.0, ppq);
    rig.run(std::vector<float>(32, 0.0f));
    return { rig.lastControlOut[0], rig.lastControlOut[1], rig.lastControlOut[2], rig.lastControlOut[3] };
}

ElementTestFixture player(const char* pattern)
{
    ElementTestFixture rig("StepSequencer", kRate);
    std::string error;
    const bool ok = rig.element->setOption("pattern", pattern, error);
    assert(ok);
    return rig;
}

void testStepsFollowThePosition()
{
    auto rig = player("C4 . E4 F#2");
    rig.set("division", 0.25f);    // a sixteenth a step

    auto o = at(rig, 0.0);
    assert(o.gate == 1.0f && o.note == 60.0f && o.step == 0.0f);
    assert(std::abs(o.pitch - 261.6256f) < 0.01f);

    o = at(rig, 0.25);             // a rest has no gate
    assert(o.gate == 0.0f && o.step == 1.0f);

    o = at(rig, 0.5);
    assert(o.gate == 1.0f && o.note == 64.0f);

    o = at(rig, 0.75);
    assert(o.gate == 1.0f && o.note == 42.0f);   // F#2

    // And it wraps, a bar later and a long way later, and before zero.
    assert(at(rig, 1.0).note == 60.0f);
    assert(at(rig, 100.5).note == 64.0f);
    assert(at(rig, -0.25).step == 3.0f);
}

void testGateLengthIsAFractionOfTheStep()
{
    auto rig = player("A3");
    rig.set("gatelength", 0.5f);
    rig.set("division", 1.0f);

    assert(at(rig, 0.25).gate == 1.0f);
    assert(at(rig, 0.49).gate == 1.0f);
    assert(at(rig, 0.51).gate == 0.0f);
    assert(at(rig, 1.1).gate == 1.0f);   // and again on the next step
}

void testTransposeAndRange()
{
    auto rig = player("C4 G9");
    rig.set("transpose", 12.0f);
    assert(at(rig, 0.0).note == 72.0f);
    assert(at(rig, 0.25).note == 127.0f);   // clamped, not wrapped
}

void testNumbersFlatsAndRests()
{
    auto rig = player("60 Bb3 - 0");
    assert(at(rig, 0.0).note == 60.0f);
    assert(at(rig, 0.25).note == 58.0f);
    assert(at(rig, 0.5).gate == 0.0f);
    assert(at(rig, 0.75).note == 0.0f && at(rig, 0.75).gate == 1.0f);
}

void testBadPatternsFailWithAReason()
{
    ElementTestFixture rig("StepSequencer", kRate);
    for (const char* bad : { "", "   ", "C4 H4", "C4 200", "C4 E", "X", "C#" })
    {
        std::string error;
        assert(! rig.element->setOption("pattern", bad, error));
        assert(! error.empty());
    }

    std::string error;
    std::string tooLong;
    for (int i = 0; i < 65; ++i) tooLong += "C4 ";
    assert(! rig.element->setOption("pattern", tooLong, error));
    assert(rig.element->setOption("something", "else", error));   // other keys are not failures
}

void testRunsOnItsOwnWhenStopped()
{
    auto rig = player("C4 E4");
    rig.set("division", 0.25f);
    rig.setTransport(false, 120.0, 0.0);

    // At 120 bpm a sixteenth is 125 ms: 6000 samples. After many blocks it has moved on.
    std::vector<float> steps;
    for (int i = 0; i < 400; ++i)
    {
        rig.run(std::vector<float>(480, 0.0f));
        steps.push_back(rig.lastControlOut[3]);
    }
    int changes = 0;
    for (std::size_t i = 1; i < steps.size(); ++i)
        if (steps[i] != steps[i - 1]) ++changes;
    std::printf("  stopped, 4 s at 120 bpm: %d step changes (expect about 32)\n", changes);
    assert(changes >= 30 && changes <= 34);
}

}  // namespace

int main()
{
    testStepsFollowThePosition();
    testGateLengthIsAFractionOfTheStep();
    testTransposeAndRange();
    testNumbersFlatsAndRests();
    testBadPatternsFailWithAReason();
    testRunsOnItsOwnWhenStopped();

    std::printf("StepSequencerTest PASSED\n");
    return 0;
}

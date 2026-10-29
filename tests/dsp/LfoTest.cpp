// tests/dsp/LfoTest.cpp
//
// val:LFO's retrigger, host sync and drawn curve. The older shapes are
// covered by the circuits that use them.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

float step(ElementTestFixture& rig, int samples = 480)
{
    rig.run(std::vector<float>(static_cast<std::size_t>(samples), 0.0f));
    return rig.lastControlOut[0];
}

void testRetriggerRestartsTheCycle()
{
    ElementTestFixture rig("LFO", kRate);
    rig.set("rate", 1.0f);
    rig.set("shape", 2.0f);   // saw: 0 at the start of a cycle, rising to 1

    // Run most of a cycle, then retrigger: the next value is the start again.
    for (int i = 0; i < 80; ++i) step(rig);
    const float late = step(rig);
    assert(late > 0.5f);

    rig.set("retrigger", 1.0f);
    const float restarted = step(rig);
    assert(restarted < -0.99f);

    // Held high, it does not keep restarting: the saw rises.
    const float after = step(rig, 4800);
    assert(after > restarted);

    // And a second rise restarts it again.
    rig.set("retrigger", 0.0f);
    step(rig, 4800);
    rig.set("retrigger", 1.0f);
    assert(step(rig) < -0.99f);
}

void testSyncFollowsTheHostPosition()
{
    ElementTestFixture rig("LFO", kRate);
    rig.set("shape", 2.0f);
    rig.set("sync", 4.0f);   // one cycle every four beats

    rig.setTransport(true, 120.0, 0.0);
    assert(std::abs(step(rig) - -1.0f) < 1.0e-4f);

    rig.setTransport(true, 120.0, 1.0);   // a quarter of the way round
    assert(std::abs(step(rig) - -0.5f) < 1.0e-4f);

    rig.setTransport(true, 120.0, 6.0);   // 1.5 cycles
    assert(std::abs(step(rig) - 0.0f) < 1.0e-4f);

    // Stopped, it free-runs at the rate rather than sticking.
    rig.setTransport(false, 120.0, 0.0);
    rig.set("rate", 10.0f);
    const float a = step(rig);
    const float b = step(rig);
    assert(a != b);
}

void testCurveInterpolatesAndWraps()
{
    ElementTestFixture rig("LFO", kRate);
    std::string error;
    const bool ok = rig.element->setOption("curve", "0, 1 -1", error);
    assert(ok);

    rig.set("shape", 5.0f);
    rig.set("sync", 3.0f);   // three points over three beats: one per beat

    rig.setTransport(true, 120.0, 0.0);
    assert(std::abs(step(rig) - 0.0f) < 1.0e-4f);
    rig.setTransport(true, 120.0, 0.5);   // halfway from 0 to 1
    assert(std::abs(step(rig) - 0.5f) < 1.0e-4f);
    rig.setTransport(true, 120.0, 1.0);
    assert(std::abs(step(rig) - 1.0f) < 1.0e-4f);
    rig.setTransport(true, 120.0, 2.5);   // halfway from -1 back round to the first point, 0
    assert(std::abs(step(rig) - -0.5f) < 1.0e-4f);
}

void testBadCurvesFailWithAReason()
{
    ElementTestFixture rig("LFO", kRate);
    const char* bad[] = { "", "0 banana 1", "0 1 ;" };
    for (const char* text : bad)
    {
        std::string error;
        assert(! rig.element->setOption("curve", text, error));
        assert(! error.empty());
    }

    std::string error;
    assert(rig.element->setOption("curve", "5 -5", error));   // out of range is clamped, not refused
    rig.set("shape", 5.0f);
    rig.set("sync", 2.0f);
    rig.setTransport(true, 120.0, 0.0);
    assert(step(rig) == 1.0f);

    // Without a curve the shape is silent rather than undefined.
    ElementTestFixture bare("LFO", kRate);
    bare.set("shape", 5.0f);
    assert(step(bare) == 0.0f);
}

}  // namespace

int main()
{
    testRetriggerRestartsTheCycle();
    testSyncFollowsTheHostPosition();
    testCurveInterpolatesAndWraps();
    testBadCurvesFailWithAReason();

    std::printf("LfoTest PASSED\n");
    return 0;
}

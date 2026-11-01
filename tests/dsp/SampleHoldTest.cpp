// tests/dsp/SampleHoldTest.cpp
//
// val:SampleHold. A control element runs once per 32-sample slice, so the test calls the fixture once per slice
// and reads the control output.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

float slice(ElementTestFixture& rig)
{
    rig.run(std::vector<float>(32, 0.0f));
    return rig.lastControlOut[0];
}

} // namespace

int main()
{
    ElementTestFixture rig("SampleHold", 48000.0);

    // Nothing has triggered: the output is 0 whatever the input is.
    rig.set("in", 0.7f);
    assert(slice(rig) == 0.0f);

    // A rise copies the input in that slice.
    rig.set("trigger", 1.0f);
    assert(std::abs(slice(rig) - 0.7f) < 1.0e-6f);

    // The input moves on and the trigger stays high or falls: the copy holds, for many slices.
    rig.set("in", 0.2f);
    for (int i = 0; i < 10; ++i)
        assert(std::abs(slice(rig) - 0.7f) < 1.0e-6f);
    rig.set("trigger", 0.0f);
    for (int i = 0; i < 100; ++i)
        assert(std::abs(slice(rig) - 0.7f) < 1.0e-6f);

    // The next rise takes the new value.
    rig.set("trigger", 1.0f);
    assert(std::abs(slice(rig) - 0.2f) < 1.0e-6f);

    // A trigger that never passes 0.5 does nothing.
    rig.set("trigger", 0.0f);
    slice(rig);
    rig.set("in", 0.9f);
    rig.set("trigger", 0.4f);
    assert(std::abs(slice(rig) - 0.2f) < 1.0e-6f);

    // Reset forgets.
    rig.reset();
    assert(slice(rig) == 0.0f);

    std::puts("SampleHoldTest: ok");
    return 0;
}

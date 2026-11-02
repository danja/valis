// tests/dsp/MidiCCTest.cpp
//
// val:MidiCC. Until a controller is received the output is val:initial; after it, the value; the controller
// number picks which one; and a controller outside 0 to 127, or none supplied, falls back to val:initial.

#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace valis;

namespace {

float read(ElementTestFixture& rig)
{
    rig.run(std::vector<float>(32, 0.0f));
    assert(rig.lastControlOut.size() >= 1);
    return rig.lastControlOut[0];
}

}  // namespace

int main()
{
    // not received: the resting value
    ElementTestFixture rig("MidiCC");
    assert(read(rig) == 0.0f);
    rig.set("initial", 0.3f);
    assert(std::abs(read(rig) - 0.3f) < 1e-6f);

    // controller 1 (the default) arrives
    rig.setController(1, 64.0f / 127.0f);
    assert(std::abs(read(rig) - 64.0f / 127.0f) < 1e-6f);

    // another controller does not move it, choosing it does
    rig.setController(2, 1.0f);
    assert(std::abs(read(rig) - 64.0f / 127.0f) < 1e-6f);
    rig.set("controller", 2.0f);
    assert(read(rig) == 1.0f);

    // one that was never sent reads as the resting value, at either end of the range
    rig.set("controller", 4.0f);
    assert(std::abs(read(rig) - 0.3f) < 1e-6f);
    rig.set("controller", 127.0f);
    assert(std::abs(read(rig) - 0.3f) < 1e-6f);
    rig.set("controller", 0.0f);
    assert(std::abs(read(rig) - 0.3f) < 1e-6f);

    // zero is a value, not "not received"
    rig.setController(0, 0.0f);
    assert(read(rig) == 0.0f);

    std::puts("MidiCC ok");
    return 0;
}

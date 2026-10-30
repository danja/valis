// tests/dsp/ControlMathTest.cpp
//
// val:ControlAdd and val:Slew. Control elements run once per slice, so a test
// calls the fixture once per 32-sample slice and reads the control output.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate  = 48000.0;
constexpr int    kSlice = 32;

float slice(ElementTestFixture& rig)
{
    rig.run(std::vector<float>(static_cast<std::size_t>(kSlice), 0.0f));
    return rig.lastControlOut[0];
}

void testControlAddSumsItsInputs()
{
    ElementTestFixture rig("ControlAdd", kRate);
    assert(slice(rig) == 0.0f);                    // nothing set: adds nothing

    rig.set("a", 220.0f);
    rig.set("b", -7.5f);
    assert(std::abs(slice(rig) - 212.5f) < 1.0e-4f);

    rig.set("c", 100.0f);
    assert(std::abs(slice(rig) - 312.5f) < 1.0e-4f);
}

void testSlewStartsAtTheInput()
{
    ElementTestFixture rig("Slew", kRate);
    rig.set("in", 440.0f);
    rig.set("rise", 500.0f);
    assert(slice(rig) == 440.0f);                  // no glide up from zero
}

/// The output covers 90% of a step in the stated time, rising or falling, and
/// the two directions are independent.
void testSlewTakesTheStatedTime()
{
    ElementTestFixture rig("Slew", kRate);
    rig.set("in", 0.0f);
    rig.set("rise", 100.0f);
    rig.set("fall", 400.0f);
    slice(rig);

    rig.set("in", 1.0f);
    float value = 0.0f;
    const int risingSlices = static_cast<int>(0.100 * kRate / kSlice);
    for (int i = 0; i < risingSlices; ++i)
        value = slice(rig);
    std::printf("  slew: rising 90%% mark reads %.3f\n", value);
    assert(std::abs(value - 0.9f) < 0.02f);

    for (int i = 0; i < 400; ++i)
        value = slice(rig);
    assert(value > 0.999f);

    rig.set("in", 0.0f);
    const int fallingSlices = static_cast<int>(0.400 * kRate / kSlice);
    for (int i = 0; i < fallingSlices; ++i)
        value = slice(rig);
    std::printf("  slew: falling 10%% mark reads %.3f\n", value);
    assert(std::abs(value - 0.1f) < 0.02f);
}

void testSlewOfZeroFollowsAtOnce()
{
    ElementTestFixture rig("Slew", kRate);
    rig.set("in", 1.0f);
    rig.set("rise", 0.0f);
    rig.set("fall", 0.0f);
    slice(rig);
    rig.set("in", 5.0f);
    assert(slice(rig) == 5.0f);
    rig.set("in", -3.0f);
    assert(slice(rig) == -3.0f);
}

void testSlewResetTakesTheInputAgain()
{
    ElementTestFixture rig("Slew", kRate);
    rig.set("rise", 1000.0f);
    rig.set("in", 1.0f);
    slice(rig);
    rig.set("in", 2.0f);
    assert(slice(rig) < 1.1f);                     // still on its way
    rig.reset();
    assert(slice(rig) == 2.0f);
}

}  // namespace

int main()
{
    testControlAddSumsItsInputs();
    testSlewStartsAtTheInput();
    testSlewTakesTheStatedTime();
    testSlewOfZeroFollowsAtOnce();
    testSlewResetTakesTheInputAgain();
    std::printf("ControlMathTest passed\n");
    return 0;
}

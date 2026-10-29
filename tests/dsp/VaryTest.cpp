// tests/dsp/VaryTest.cpp
//
// val:Vary. A random sample-and-hold is judged by what it holds between
// triggers, whether the values stay in range, and whether a seed repeats.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

using namespace valis;

namespace {

/// One trigger pulse through the element: low, high, low. The value after it.
float hit(ElementTestFixture& rig)
{
    rig.set("trigger", 0.0f); rig.run({});
    rig.set("trigger", 1.0f); rig.run({});
    const float value = rig.lastControlOut[0];
    rig.set("trigger", 0.0f); rig.run({});
    return value;
}

void testRestsAtTheMiddleUntilTriggered()
{
    ElementTestFixture rig("Vary");
    rig.set("min", 10.0f);
    rig.set("max", 20.0f);
    rig.run({});
    assert(std::abs(rig.lastControlOut[0] - 15.0f) < 1.0e-5f);
}

void testHoldsBetweenTriggersAndStaysInRange()
{
    ElementTestFixture rig("Vary");
    rig.set("min", -2.0f);
    rig.set("max", 3.0f);

    std::set<float> seen;
    for (int i = 0; i < 200; ++i)
    {
        const float v = hit(rig);
        assert(v >= -2.0f && v <= 3.0f);
        seen.insert(v);

        // Held with the trigger low, and while it stays high.
        rig.run({});
        assert(rig.lastControlOut[0] == v);
    }
    assert(seen.size() > 150);   // continuous, so nearly every draw is new
}

void testStepsGiveEvenlySpacedValues()
{
    ElementTestFixture rig("Vary");
    rig.set("min", 0.0f);
    rig.set("max", 4.0f);
    rig.set("steps", 5.0f);

    std::set<float> seen;
    for (int i = 0; i < 300; ++i)
        seen.insert(hit(rig));

    assert(seen.size() == 5);
    for (const float v : seen)
        assert(std::abs(v - std::round(v)) < 1.0e-5f);
    assert(*seen.begin() == 0.0f && *seen.rbegin() == 4.0f);
}

void testProbabilitySkipsHitsWithoutReshufflingTheRest()
{
    ElementTestFixture all("Vary"), some("Vary");
    some.set("probability", 0.5f);

    std::vector<float> every, kept;
    for (int i = 0; i < 400; ++i)
    {
        every.push_back(hit(all));
        kept.push_back(hit(some));
    }

    int changes = 0;
    for (std::size_t i = 1; i < kept.size(); ++i)
    {
        if (kept[i] != kept[i - 1])
        {
            ++changes;
            // Whenever the half-probability one takes a hit it takes the value
            // the certain one drew on that same hit.
            assert(kept[i] == every[i]);
        }
    }
    std::printf("  probability 0.5: %d of 400 hits changed the value\n", changes);
    assert(changes > 150 && changes < 250);

    ElementTestFixture never("Vary");
    never.set("probability", 0.0f);
    for (int i = 0; i < 20; ++i)
        assert(std::abs(hit(never) - 0.5f) < 1.0e-5f);
}

void testSeedsRepeatAndDiffer()
{
    const auto sequence = [](float seed)
    {
        ElementTestFixture rig("Vary");
        rig.set("seed", seed);
        std::vector<float> values;
        for (int i = 0; i < 16; ++i)
            values.push_back(hit(rig));
        return values;
    };

    assert(sequence(7.0f) == sequence(7.0f));
    assert(sequence(7.0f) != sequence(8.0f));

    // A reset puts the sequence back at its start.
    ElementTestFixture rig("Vary");
    const float first = hit(rig);
    hit(rig);
    rig.reset();
    assert(hit(rig) == first);
}

}  // namespace

int main()
{
    testRestsAtTheMiddleUntilTriggered();
    testHoldsBetweenTriggersAndStaysInRange();
    testStepsGiveEvenlySpacedValues();
    testProbabilitySkipsHitsWithoutReshufflingTheRest();
    testSeedsRepeatAndDiffer();

    std::printf("VaryTest PASSED\n");
    return 0;
}

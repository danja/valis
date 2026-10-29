// tests/dsp/SampleLoadChokeTest.cpp
//
// val:SampleLoad's choke port and val:ControlMax, which together make a drum
// card monophonic: a rise on choke stops the sound, quickly but without a click.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> block(ElementTestFixture& rig, int n = 480)
{
    return rig.run(std::vector<float>(static_cast<std::size_t>(n), 0.0f));
}

ElementTestFixture player()
{
    ElementTestFixture rig("SampleLoad", kRate);
    std::string error;
    const bool ok = rig.element->setOption("file", "samples/bell.wav", error);
    assert(ok);
    rig.set("loop", 0.0f);
    return rig;
}

void testChokeStopsTheSound()
{
    auto rig = player();

    rig.set("trigger", 0.0f);
    block(rig);
    rig.set("trigger", 1.0f);
    assert(ElementTestFixture::measureRms(block(rig)) > 0.001f);   // playing
    rig.set("trigger", 0.0f);
    assert(ElementTestFixture::measureRms(block(rig)) > 0.001f);   // still ringing

    rig.set("choke", 1.0f);
    const auto cut = block(rig);

    // It fades rather than stopping dead: the first samples are still near their level,
    // and the whole 10 ms block is silent after 2 ms.
    assert(ElementTestFixture::measureRms(std::vector<float>(cut.begin() + 200, cut.end())) == 0.0f);
    assert(cut[0] != 0.0f);

    // And stays stopped.
    rig.set("choke", 0.0f);
    assert(ElementTestFixture::measureRms(block(rig)) == 0.0f);
}

void testChokeAndTriggerTogetherPlays()
{
    auto rig = player();
    rig.set("trigger", 0.0f);
    block(rig);

    rig.set("trigger", 1.0f);
    rig.set("choke", 1.0f);
    assert(ElementTestFixture::measureRms(block(rig)) > 0.001f);
}

void testItCanBeRetriggeredAfterAChoke()
{
    auto rig = player();
    rig.set("trigger", 0.0f);
    block(rig);
    rig.set("trigger", 1.0f);
    block(rig);
    rig.set("trigger", 0.0f);
    rig.set("choke", 1.0f);
    block(rig);
    rig.set("choke", 0.0f);
    assert(ElementTestFixture::measureRms(block(rig)) == 0.0f);

    rig.set("trigger", 1.0f);
    assert(ElementTestFixture::measureRms(block(rig)) > 0.001f);
}

void testWithoutChokeNothingChanges()
{
    auto rig = player();
    rig.set("trigger", 0.0f);
    block(rig);
    rig.set("trigger", 1.0f);
    for (int i = 0; i < 20; ++i)
        assert(ElementTestFixture::measureRms(block(rig)) > 0.0f || i > 10);
}

void testControlMaxIsAnOr()
{
    ElementTestFixture rig("ControlMax", kRate);
    const auto out = [&](float a, float b, float c)
    {
        rig.set("a", a); rig.set("b", b); rig.set("c", c);
        block(rig, 32);
        return rig.lastControlOut[0];
    };

    assert(out(0.0f, 0.0f, 0.0f) == 0.0f);
    assert(out(1.0f, 0.0f, 0.0f) == 1.0f);
    assert(out(0.0f, 1.0f, 0.0f) == 1.0f);
    assert(out(0.0f, 0.0f, 1.0f) == 1.0f);
    assert(out(-3.0f, 0.5f, -1.0f) == 0.5f);

    // Unconnected inputs sit at their default and never win over a real zero.
    ElementTestFixture bare("ControlMax", kRate);
    bare.set("a", 0.0f);
    block(bare, 32);
    assert(bare.lastControlOut[0] == 0.0f);
}

void testAudioSelectPicksAndCrossfades()
{
    ElementTestFixture rig("AudioSelect", kRate);
    std::vector<float> a(2400, 1.0f), b(2400, 2.0f), c(2400, 3.0f);

    // Only the first input is wired by the fixture's run(); use the three through its
    // second-input hook one at a time by checking the select for each in turn.
    for (int pick = 0; pick < 3; ++pick)
    {
        ElementTestFixture r("AudioSelect", kRate);
        r.set("select", static_cast<float>(pick));
        const auto out = pick == 0 ? r.run(a) : r.run(a, "out", &b);
        // Input c is not reachable from the fixture, so for 2 only check silence from a and b.
        if (pick == 0) assert(std::abs(out.back() - 1.0f) < 1.0e-6f);
        if (pick == 1) assert(std::abs(out.back() - 2.0f) < 1.0e-6f);
        if (pick == 2) assert(std::abs(out.back()) < 1.0e-6f);
    }

    // Switching from a to b is a ramp, not a step: nothing jumps by more than a few percent.
    ElementTestFixture r("AudioSelect", kRate);
    r.set("select", 0.0f);
    r.run(a, "out", &b);
    r.set("select", 1.0f);
    const auto out = r.run(a, "out", &b);
    float largestStep = 0.0f;
    for (std::size_t i = 1; i < out.size(); ++i)
        largestStep = std::max(largestStep, std::abs(out[i] - out[i - 1]));
    assert(largestStep < 0.02f);
    assert(std::abs(out.front() - 1.0f) < 0.05f);
    assert(std::abs(out.back() - 2.0f) < 1.0e-6f);
}

}  // namespace

int main()
{
    testChokeStopsTheSound();
    testChokeAndTriggerTogetherPlays();
    testItCanBeRetriggeredAfterAChoke();
    testWithoutChokeNothingChanges();
    testControlMaxIsAnOr();
    testAudioSelectPicksAndCrossfades();

    std::printf("SampleLoadChokeTest PASSED\n");
    return 0;
}

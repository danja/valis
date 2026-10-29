// tests/dsp/CompressorTest.cpp
//
// val:Compressor's upward stage. The downward stage is covered by the behaviour
// test; what is checked here is that lifting quiet signals has a ceiling on how
// much it lifts, since without one the quiet end of a passage is the loudest
// thing in the mix.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

/// Gain in dB on a steady sine of the given level, once the detector has settled.
double gainDb(ElementTestFixture& rig, double levelDb)
{
    rig.reset();
    const float amplitude = std::pow(10.0f, static_cast<float>(levelDb) / 20.0f);
    const auto out = rig.run(ElementTestFixture::generateSine(440.0, kRate, 24000, amplitude));
    const auto in  = ElementTestFixture::generateSine(440.0, kRate, 24000, amplitude);

    const std::vector<float> outTail(out.begin() + 12000, out.end());
    const std::vector<float> inTail(in.begin() + 12000, in.end());
    return 20.0 * std::log10(ElementTestFixture::measureRms(outTail) / ElementTestFixture::measureRms(inTail));
}

ElementTestFixture upwardRig(float boost)
{
    ElementTestFixture rig("Compressor", kRate);
    rig.set("threshold", -20.0f);
    rig.set("ratio", 4.0f);
    rig.set("knee", 0.0f);
    rig.set("attack", 0.1f);
    rig.set("release", 10.0f);
    rig.set("upward", 1.0f);
    rig.set("boost", boost);
    return rig;
}

void testBoostIsCapped()
{
    // At -80 dB with a -20 dB threshold and 4:1, the uncapped lift is
    // 60 x 0.75 = 45 dB. The cap decides what is actually applied.
    for (const float boost : { 6.0f, 18.0f, 30.0f })
    {
        auto rig = upwardRig(boost);
        const double g = gainDb(rig, -80.0);
        std::printf("  boost cap %.0f dB: lifted a -80 dB signal by %.2f dB\n", boost, g);
        assert(std::abs(g - boost) < 0.5);
    }
}

void testSmallLiftsAreUnaffectedByTheCap()
{
    // -30 dB is 10 below the threshold: a lift of 7.5 dB, under every cap here.
    auto rig = upwardRig(18.0f);
    const double g = gainDb(rig, -30.0);
    std::printf("  -30 dB signal lifted by %.2f dB (expected 7.5)\n", g);
    assert(std::abs(g - 7.5) < 0.5);
}

void testCapDoesNotTouchTheDownwardStage()
{
    auto rig = upwardRig(0.0f);
    // -8 dB is 12 over the threshold: 12 x 0.75 = 9 dB of reduction.
    const double g = gainDb(rig, -8.0);
    std::printf("  -8 dB signal changed by %.2f dB (expected -9)\n", g);
    assert(std::abs(g + 9.0) < 0.5);
}

void testLiftHasItsOwnThreshold()
{
    // Threshold -20, lift threshold -50. A -30 dB signal is below the first and
    // above the second: not lifted. A -60 dB one is lifted from -50: 10 x 0.75.
    auto rig = upwardRig(60.0f);
    rig.set("liftThreshold", -50.0f);

    const double between = gainDb(rig, -30.0);
    const double below   = gainDb(rig, -60.0);
    std::printf("  lift threshold -50: -30 dB changed by %.2f dB, -60 dB lifted by %.2f dB\n", between, below);
    assert(std::abs(between) < 0.3);
    assert(std::abs(below - 7.5) < 0.5);

    // The downward stage is still on the main threshold.
    assert(std::abs(gainDb(rig, -8.0) + 9.0) < 0.5);

    // The lowest setting follows the threshold, as before the port existed.
    rig.set("liftThreshold", -90.0f);
    assert(std::abs(gainDb(rig, -30.0) - 7.5) < 0.5);
}

}  // namespace

int main()
{
    testBoostIsCapped();
    testSmallLiftsAreUnaffectedByTheCap();
    testCapDoesNotTouchTheDownwardStage();
    testLiftHasItsOwnThreshold();

    std::printf("CompressorTest PASSED\n");
    return 0;
}

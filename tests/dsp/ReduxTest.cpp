// tests/dsp/ReduxTest.cpp
//
// val:Redux. Reducing bits must leave only the levels a word that size can
// hold; reducing the rate must hold values and alias; jitter must vary the hold
// without losing the rate or the repeatability; and with both off the input
// must come through untouched.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi   = 3.14159265358979323846;

std::vector<float> ramp(int n)
{
    std::vector<float> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        out[static_cast<std::size_t>(i)] = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(n);
    return out;
}

void testOffIsAWire()
{
    ElementTestFixture rig("Redux", kRate);
    rig.set("bits", 24.0f);
    rig.set("downsample", 1.0f);

    const auto in  = ElementTestFixture::generateSine(777.0, kRate, 4096, 0.7f);
    const auto out = rig.run(in);
    for (std::size_t i = 0; i < in.size(); ++i)
        assert(out[i] == in[i]);
}

void testBitDepthLimitsTheLevels()
{
    for (const int bits : { 2, 4, 8 })
    {
        ElementTestFixture rig("Redux", kRate);
        rig.set("bits", static_cast<float>(bits));
        rig.set("downsample", 1.0f);

        std::set<float> levels;
        for (float s : rig.run(ramp(20000)))
            levels.insert(s);

        // 2^(bits-1) steps either side of zero, so 2^bits + 1 levels at most.
        const std::size_t most = (std::size_t{1} << bits) + 1;
        std::printf("  %d bits: %zu levels (at most %zu)\n", bits, levels.size(), most);
        assert(levels.size() <= most);
        assert(levels.size() >= most / 2);
    }
}

void testDownsampleHoldsValues()
{
    ElementTestFixture rig("Redux", kRate);
    rig.set("bits", 24.0f);
    rig.set("downsample", 4.0f);

    const auto in  = ElementTestFixture::generateSine(1000.0, kRate, 4096, 0.5f);
    const auto out = rig.run(in);
    for (std::size_t i = 0; i + 3 < out.size(); i += 4)
    {
        assert(out[i] == out[i + 1] && out[i] == out[i + 2] && out[i] == out[i + 3]);
        assert(out[i] == in[i]);
    }
}

void testDownsampleAliases()
{
    // A 10 kHz sine sampled at 12 kHz folds to 2 kHz.
    ElementTestFixture rig("Redux", kRate);
    rig.set("bits", 24.0f);
    rig.set("downsample", 4.0f);

    const auto out  = rig.run(ElementTestFixture::generateSine(10000.0, kRate, 4096, 0.5f));
    const auto bins = ElementTestFixture::computeSpectrum(out, 12);
    const float alias = ElementTestFixture::measureEnergyAt(bins, 2000.0, kRate);
    const float tone  = ElementTestFixture::measureEnergyAt(bins, 10000.0, kRate);
    std::printf("  alias at 2 kHz %.3g, tone at 10 kHz %.3g\n", alias, tone);
    assert(alias > 0.1f * tone);
}

void testJitterVariesTheHoldButKeepsTheRate()
{
    ElementTestFixture rig("Redux", kRate);
    rig.set("bits", 24.0f);
    rig.set("downsample", 8.0f);
    rig.set("jitter", 1.0f);

    // A strictly rising input shows each hold as a run of one value.
    const int n = 40000;
    std::vector<float> in(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        in[static_cast<std::size_t>(i)] = static_cast<float>(i) / static_cast<float>(n);

    const auto out = rig.run(in);
    std::set<int> lengths;
    int holds = 0, run = 1;
    for (std::size_t i = 1; i < out.size(); ++i)
    {
        if (out[i] == out[i - 1])
            ++run;
        else
        {
            lengths.insert(run);
            ++holds;
            run = 1;
        }
    }

    const double mean = static_cast<double>(n) / holds;
    std::printf("  %zu distinct hold lengths, mean %.2f (asked for 8)\n", lengths.size(), mean);
    assert(lengths.size() >= 4);
    assert(std::abs(mean - 8.0) < 0.5);

    // Same seed, same result after a reset.
    rig.reset();
    const auto again = rig.run(in);
    assert(again == out);
}

}  // namespace

int main()
{
    testOffIsAWire();
    testBitDepthLimitsTheLevels();
    testDownsampleHoldsValues();
    testDownsampleAliases();
    testJitterVariesTheHoldButKeepsTheRate();

    std::printf("ReduxTest PASSED\n");
    return 0;
}

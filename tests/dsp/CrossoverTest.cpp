// tests/dsp/CrossoverTest.cpp
//
// val:Crossover. The promise of a Linkwitz-Riley split is that the two sides add
// back to a flat response, with each side 6 dB down at the corner and steep
// beyond it. Measured with sines at the frequencies a mix would have.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi   = 3.14159265358979323846;

double amplitudeAt(const std::vector<float>& signal, std::size_t from, double hz)
{
    double re = 0.0, im = 0.0;
    for (std::size_t i = from; i < signal.size(); ++i)
    {
        const double phase = 2.0 * kPi * hz * static_cast<double>(i) / kRate;
        re += signal[i] * std::cos(phase);
        im += signal[i] * std::sin(phase);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(signal.size() - from);
}

double db(double x) { return 20.0 * std::log10(std::max(x, 1.0e-12)); }

struct Split { double low, high, sum; };

/// Levels of both outputs and of their sum at one frequency. A second of sine,
/// the last half measured; the frequencies used fit whole cycles in it.
Split measure(double corner, double hz)
{
    ElementTestFixture rig("Crossover", kRate);
    rig.set("frequency", static_cast<float>(corner));

    const int total = static_cast<int>(kRate);
    const auto in   = ElementTestFixture::generateSine(hz, kRate, total, 1.0f);
    const auto low  = rig.run(in, "low");
    rig.reset();
    const auto high = rig.run(in, "high");

    std::vector<float> sum(low.size());
    for (std::size_t i = 0; i < sum.size(); ++i)
        sum[i] = low[i] + high[i];

    const std::size_t from = static_cast<std::size_t>(total / 2);
    return { db(amplitudeAt(low, from, hz)), db(amplitudeAt(high, from, hz)),
             db(amplitudeAt(sum, from, hz)) };
}

void testSumIsFlat()
{
    for (const double corner : { 200.0, 1000.0, 5000.0 })
        for (const double hz : { 50.0, 100.0, 400.0, 1000.0, 2000.0, 6000.0, 12000.0 })
        {
            const auto m = measure(corner, hz);
            if (std::abs(m.sum) >= 0.05)
                std::printf("  corner %.0f, %.0f Hz: low %.2f high %.2f sum %.3f dB\n",
                            corner, hz, m.low, m.high, m.sum);
            assert(std::abs(m.sum) < 0.05);
        }
}

void testSidesAreSixDownAtTheCorner()
{
    const auto m = measure(1000.0, 1000.0);
    std::printf("  at the corner: low %.2f dB, high %.2f dB\n", m.low, m.high);
    assert(std::abs(m.low  + 6.02) < 0.1);
    assert(std::abs(m.high + 6.02) < 0.1);
}

void testSlopeIs24DbPerOctave()
{
    // Two octaves past the corner, an analogue 4th-order filter is 48 dB down plus the
    // 6 dB at the corner itself less the roll-off already counted: 24 dB per octave
    // asymptotically, so a little over 48 dB at 4x.
    const auto low = measure(1000.0, 4000.0);
    const auto high = measure(1000.0, 250.0);
    std::printf("  low side at 4x: %.1f dB, high side at 1/4: %.1f dB\n", low.low, high.high);
    assert(low.low < -46.0 && low.low > -56.0);
    assert(high.high < -46.0 && high.high > -56.0);

    // And the other side is untouched far into its own band.
    assert(low.high > -0.3);
    assert(high.low > -0.3);
}

}  // namespace

int main()
{
    testSumIsFlat();
    testSidesAreSixDownAtTheCorner();
    testSlopeIs24DbPerOctave();

    std::printf("CrossoverTest PASSED\n");
    return 0;
}

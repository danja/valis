// tests/dsp/BiquadTest.cpp
//
// val:Biquad. A filter is checked by its frequency response, so each mode is
// measured with sines against what the cookbook says it must do: the gain at the
// corner, the gain well inside the stopband or boost, and the gain well away.

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

enum Mode { lowShelf = 0, bell, highShelf, notch, lowCut, highCut };

/// Gain in dB at one frequency. The sine runs for a second, and the last half
/// second, a whole number of cycles for every frequency used here, is measured.
double gainDb(ElementTestFixture& rig, double hz)
{
    rig.reset();
    const int total = static_cast<int>(kRate);
    const auto out = rig.run(ElementTestFixture::generateSine(hz, kRate, total, 1.0f));

    double re = 0.0, im = 0.0;
    const std::size_t from = static_cast<std::size_t>(total / 2);
    for (std::size_t i = from; i < out.size(); ++i)
    {
        const double phase = 2.0 * kPi * hz * static_cast<double>(i) / kRate;
        re += out[i] * std::cos(phase);
        im += out[i] * std::sin(phase);
    }
    const double amplitude = 2.0 * std::hypot(re, im) / static_cast<double>(out.size() - from);
    return 20.0 * std::log10(std::max(amplitude, 1.0e-12));
}

ElementTestFixture make(Mode mode, double frequency, double gain = 0.0, double q = 0.7071)
{
    ElementTestFixture rig("Biquad", kRate);
    rig.set("mode", static_cast<float>(mode));
    rig.set("frequency", static_cast<float>(frequency));
    rig.set("gain", static_cast<float>(gain));
    rig.set("q", static_cast<float>(q));
    return rig;
}

void near(const char* what, double got, double expected, double tolerance)
{
    std::printf("  %-34s %7.2f dB (expected %.2f)\n", what, got, expected);
    assert(std::abs(got - expected) <= tolerance);
}

void testBell()
{
    auto rig = make(bell, 1000.0, 12.0, 1.0);
    near("bell +12 at its centre", gainDb(rig, 1000.0), 12.0, 0.2);
    near("bell +12 at 20 Hz", gainDb(rig, 20.0), 0.0, 0.5);
    near("bell +12 a decade above", gainDb(rig, 10000.0), 0.0, 0.8);

    auto cut = make(bell, 1000.0, -9.0, 2.0);
    near("bell -9 at its centre", gainDb(cut, 1000.0), -9.0, 0.2);
}

void testShelves()
{
    auto low = make(lowShelf, 1000.0, 12.0);
    near("low shelf +12 at 50 Hz", gainDb(low, 50.0), 12.0, 0.5);
    near("low shelf +12 at 10 kHz", gainDb(low, 10000.0), 0.0, 0.3);
    near("low shelf +12 at its corner", gainDb(low, 1000.0), 6.0, 0.3);

    auto high = make(highShelf, 1000.0, -12.0);
    near("high shelf -12 at 10 kHz", gainDb(high, 10000.0), -12.0, 0.5);
    near("high shelf -12 at 50 Hz", gainDb(high, 50.0), 0.0, 0.3);
    near("high shelf -12 at its corner", gainDb(high, 1000.0), -6.0, 0.3);
}

void testNotch()
{
    auto rig = make(notch, 1000.0, 0.0, 4.0);
    assert(gainDb(rig, 1000.0) < -60.0);
    near("notch a decade away", gainDb(rig, 10000.0), 0.0, 0.3);
    near("notch at 100 Hz", gainDb(rig, 100.0), 0.0, 0.3);
}

void testCuts()
{
    auto lc = make(lowCut, 1000.0);
    near("low cut at its corner", gainDb(lc, 1000.0), -3.01, 0.2);
    near("low cut a decade below (12 dB/oct)", gainDb(lc, 100.0), -40.0, 1.0);
    near("low cut a decade above", gainDb(lc, 10000.0), 0.0, 0.2);

    auto hc = make(highCut, 1000.0);
    near("high cut at its corner", gainDb(hc, 1000.0), -3.01, 0.2);
    // The bilinear transform squeezes the top of the spectrum, so a decade up the
    // cut is a little deeper than the analogue 40 dB, never shallower.
    const double decadeAbove = gainDb(hc, 10000.0);
    std::printf("  %-34s %7.2f dB (at least 40 down)\n", "high cut a decade above", decadeAbove);
    assert(decadeAbove < -40.0 && decadeAbove > -46.0);
    near("high cut a decade below", gainDb(hc, 100.0), 0.0, 0.2);
}

void testZeroGainIsAWire()
{
    for (Mode mode : { lowShelf, bell, highShelf })
    {
        auto rig = make(mode, 1000.0, 0.0);
        const auto in  = ElementTestFixture::generateSine(777.0, kRate, 4096, 0.5f);
        const auto out = rig.run(in);
        for (std::size_t i = 0; i < in.size(); ++i)
            assert(std::abs(out[i] - in[i]) < 1.0e-5f);
    }
}

void testLowCornerStaysStable()
{
    // A low shelf at the bottom of the range is the case single precision loses.
    auto rig = make(lowShelf, 20.0, 24.0, 0.7071);
    const auto out = rig.run(ElementTestFixture::generateSine(30.0, kRate, 48000, 0.1f));
    for (float s : out)
        assert(std::isfinite(s) && std::abs(s) < 5.0f);
}

}  // namespace

int main()
{
    testBell();
    testShelves();
    testNotch();
    testCuts();
    testZeroGainIsAWire();
    testLowCornerStaysStable();

    std::printf("BiquadTest PASSED\n");
    return 0;
}

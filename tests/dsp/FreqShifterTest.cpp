// tests/dsp/FreqShifterTest.cpp
//
// val:FreqShifter. A frequency shifter adds hertz to every partial, so a sine
// must come out at its own frequency plus the shift, with the unwanted sideband
// (the mirror image) far below it.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr int    kSize = 1 << 14;

std::vector<float> shifted(double hz, float shift, float feedback = 0.0f)
{
    ElementTestFixture rig("FreqShifter", kRate);
    rig.set("shift", shift);
    rig.set("feedback", feedback);
    const auto out = rig.run(ElementTestFixture::generateSine(hz, kRate, 48000, 0.5f));
    return std::vector<float>(out.end() - kSize, out.end());
}

/// Level of the spectrum at `hz`, the strongest of the bins either side.
float levelAt(const std::vector<float>& signal, double hz)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, 14);
    const auto centre = static_cast<std::size_t>(std::lround(hz * kSize / kRate));
    float best = 0.0f;
    for (std::size_t b = centre - 2; b <= centre + 2; ++b)
        best = std::max(best, bins[b]);
    return best;
}

void testSineMovesByTheShift()
{
    for (const float shift : { 100.0f, 437.0f, -250.0f, -800.0f })
    {
        const auto signal = shifted(1000.0, shift);
        const double wanted = 1000.0 + shift;
        const double mirror = 1000.0 - shift;
        const float tone  = levelAt(signal, wanted);
        const float image = levelAt(signal, mirror);
        const double db = 20.0 * std::log10(tone / std::max(image, 1.0e-12f));
        std::printf("  shift %+.0f Hz: tone at %.0f Hz is %.1f dB above its image\n", shift, wanted, db);
        assert(db > 30.0);
        // Nothing is left at the original frequency.
        assert(levelAt(signal, 1000.0) < tone * 0.05f);
    }
}

void testZeroShiftKeepsTheLevel()
{
    const auto signal = shifted(1500.0, 0.0f);
    const double level = ElementTestFixture::measureRms(signal) / (0.5 / std::sqrt(2.0));
    std::printf("  zero shift: level %.4f\n", level);
    assert(std::abs(level - 1.0) < 0.02);
}

void testFeedbackStaysBounded()
{
    const auto signal = shifted(1000.0, 300.0f, 0.95f);
    for (const float v : signal)
        assert(std::isfinite(v) && std::abs(v) < 20.0f);
}

}  // namespace

int main()
{
    testSineMovesByTheShift();
    testZeroShiftKeepsTheLevel();
    testFeedbackStaysBounded();

    std::printf("FreqShifterTest PASSED\n");
    return 0;
}

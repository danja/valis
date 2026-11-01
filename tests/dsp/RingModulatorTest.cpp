// tests/dsp/RingModulatorTest.cpp
//
// val:RingModulator. A sine at f against a carrier at c must come out as two sines, at c + f and c - f, with
// nothing left at f or c. The sidebands must stay put at every sample rate, and the mix control must blend
// back to the dry input.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr int kSize = 1 << 14;

std::vector<float> modulate(double rate, double hz, float carrier, float mix)
{
    ElementTestFixture rig("RingModulator", rate);
    rig.set("frequency", carrier);
    rig.set("mix", mix);
    const auto out = rig.run(ElementTestFixture::generateSine(hz, rate, static_cast<int>(rate), 0.5f));
    return std::vector<float>(out.end() - kSize, out.end());
}

/// Level at `hz`, the strongest of the bins either side.
float levelAt(const std::vector<float>& signal, double rate, double hz)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, 14);
    const auto centre = static_cast<std::size_t>(std::lround(hz * kSize / rate));
    float best = 0.0f;
    for (std::size_t b = centre - 3; b <= centre + 3; ++b)
        best = std::max(best, bins[b]);
    return best;
}

void testSidebandsAtAnyRate()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto signal = modulate(rate, 1000.0, 3000.0f, 1.0f);
        const float sum  = levelAt(signal, rate, 4000.0);
        const float diff = levelAt(signal, rate, 2000.0);
        const float dry  = levelAt(signal, rate, 1000.0);
        const float car  = levelAt(signal, rate, 3000.0);
        std::printf("rate %.0f: sum %.4f diff %.4f dry %.5f carrier %.5f\n", rate, sum, diff, dry, car);
        assert(sum > 0.1f && diff > 0.1f);
        assert(std::abs(sum - diff) < 0.02f * sum);
        assert(dry < 0.01f * sum);
        assert(car < 0.01f * sum);
    }
}

void testMixBlendsBackToTheDry()
{
    const double rate = 48000.0;
    const auto dry = modulate(rate, 1000.0, 3000.0f, 0.0f);
    assert(levelAt(dry, rate, 1000.0) > 0.1f);
    assert(levelAt(dry, rate, 4000.0) < 0.001f);

    const auto half = modulate(rate, 1000.0, 3000.0f, 0.5f);
    assert(levelAt(half, rate, 1000.0) > 0.05f);
    assert(levelAt(half, rate, 4000.0) > 0.05f);
}

void testCarrierBeyondTheLimitIsHeldBelowNyquist()
{
    const double rate = 48000.0;
    const auto signal = modulate(rate, 1000.0, 10000.0f, 1.0f);
    for (const float v : signal)
        assert(std::isfinite(v) && std::abs(v) <= 0.5f + 1.0e-4f);
}

}  // namespace

int main()
{
    testSidebandsAtAnyRate();
    testMixBlendsBackToTheDry();
    testCarrierBeyondTheLimitIsHeldBelowNyquist();
    std::puts("RingModulatorTest passed");
    return 0;
}

// tests/dsp/LimiterTest.cpp
//
// val:Limiter. A limiter makes one promise: nothing leaves above the ceiling,
// including the first peak of a signal that starts loud. It must also keep that
// promise without distorting, which is the difference between a limiter and a
// clipper, and it must leave a quiet signal exactly as it found it, one
// lookahead later.

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

void testQuietSignalIsDelayedAndUntouched()
{
    ElementTestFixture rig("Limiter", kRate);
    const int latency = rig.element->latencyInSamples();
    assert(latency > 0);

    const auto in  = ElementTestFixture::generateSine(440.0, kRate, 4096, 0.25f);
    const auto out = rig.run(in);
    for (std::size_t i = static_cast<std::size_t>(latency); i < in.size(); ++i)
        assert(out[i] == in[i - static_cast<std::size_t>(latency)]);
}

void testNothingExceedsTheCeiling()
{
    for (const float ceilingDb : { -0.3f, -6.0f, -18.0f })
    {
        ElementTestFixture rig("Limiter", kRate);
        rig.set("ceiling", ceilingDb);
        const float ceiling = std::pow(10.0f, ceilingDb / 20.0f);

        // Silence, then a sine at 8x the ceiling that starts at once: the first
        // peak is the hard case, as nothing has told the gain to fall yet.
        std::vector<float> in(1000, 0.0f);
        const auto loud = ElementTestFixture::generateSine(1000.0, kRate, 8192, 8.0f * ceiling);
        in.insert(in.end(), loud.begin(), loud.end());

        const auto out = rig.run(in);
        float peak = 0.0f;
        for (float s : out)
        {
            assert(std::isfinite(s));
            peak = std::max(peak, std::abs(s));
        }
        std::printf("  ceiling %.1f dB: peak %.4f of %.4f\n", ceilingDb, peak, ceiling);
        assert(peak <= ceiling);
        assert(peak > 0.9f * ceiling);
    }
}

void testItLimitsWithoutClipping()
{
    ElementTestFixture rig("Limiter", kRate);
    rig.set("ceiling", -6.0f);

    // 8192 samples is whole cycles of 1 kHz at 48 kHz only for the measurement
    // window below, which is taken after the gain has settled.
    const auto in  = ElementTestFixture::generateSine(1000.0, kRate, 24000, 2.0f);
    const auto out = rig.run(in);

    const std::size_t from = 12000;
    const double fundamental = amplitudeAt(out, from, 1000.0);
    const double third       = amplitudeAt(out, from, 3000.0);
    const double thirdDb     = 20.0 * std::log10(std::max(third, 1.0e-12) / fundamental);
    std::printf("  settled level %.3f, third harmonic %.1f dB\n", fundamental, thirdDb);

    // A sine 12 dB over the ceiling and hard clipped puts the third harmonic
    // near -12 dB. A limiter that has settled to a steady gain adds almost none.
    assert(std::abs(fundamental - 0.5 * 1.0) < 0.02);
    assert(thirdDb < -40.0);
}

void testGainRecoversAtTheReleaseAsked()
{
    ElementTestFixture rig("Limiter", kRate);
    rig.set("ceiling", -6.0f);
    rig.set("release", 100.0f);

    // A loud burst, then a steady 0.1 so the output over the input is the gain.
    auto in = ElementTestFixture::generateSine(1000.0, kRate, 9600, 4.0f);
    const std::size_t quietStart = in.size();
    in.insert(in.end(), static_cast<std::size_t>(0.5 * kRate), 0.1f);

    const auto out = rig.run(in);
    const auto gainAt = [&](double secondsAfterBurst)
    {
        const auto i = quietStart + static_cast<std::size_t>(secondsAfterBurst * kRate);
        return out[i] / 0.1f;
    };

    // The deficit is 1 - gain. After the window clears it falls by 1/e every
    // release time.
    const double first  = 1.0 - gainAt(0.01);
    const double second = 1.0 - gainAt(0.11);
    std::printf("  deficit %.3f then %.3f (ratio %.2f, expected 0.37)\n",
                first, second, second / first);
    assert(first > 0.1);
    assert(std::abs(second / first - 0.368) < 0.1);
    assert(gainAt(0.45) > 0.97f);
}

void testItRampsInsteadOfSnapping()
{
    ElementTestFixture rig("Limiter", kRate);
    rig.set("ceiling", -6.0f);
    const std::size_t latency = static_cast<std::size_t>(rig.element->latencyInSamples());

    // A steady 0.1 that jumps to 4.0 against a ceiling of 0.5, which needs a gain
    // of 0.125. The jump itself comes out one lookahead later, as it went in. What
    // the limiter owns is the gain while that jump is still inside the delay: the
    // 0.1 coming out must fall to 0.0125 in a ramp, and not in a step.
    const std::size_t jump = 2000;
    std::vector<float> in(jump, 0.1f);
    in.insert(in.end(), 2000, 4.0f);
    const auto out = rig.run(in);

    float largestStep = 0.0f;
    for (std::size_t i = jump + 1; i < jump + latency; ++i)
        largestStep = std::max(largestStep, std::abs(out[i] - out[i - 1]));

    std::printf("  largest gain step during the lookahead: %.5f (a snap would be 0.0875)\n", largestStep);
    assert(out[jump + latency - 1] < 0.02f);
    assert(largestStep < 0.005f);
}

void testLinkKeepsTheImage()
{
    // A pair, one side loud and one quiet. Unlinked, the quiet side passes at its
    // own level and the loud one is cut, so the balance moves. Linked, both are
    // cut by the same amount.
    constexpr float ceilingDb = -6.0f;
    const auto left  = ElementTestFixture::generateSine(1000.0, kRate, 24000, 4.0f);
    const auto right = ElementTestFixture::generateSine(1000.0, kRate, 24000, 0.1f);

    const auto limited = [&](bool linked)
    {
        ElementTestFixture l("Limiter", kRate), r("Limiter", kRate);
        l.set("ceiling", ceilingDb);
        r.set("ceiling", ceilingDb);
        const auto outL = l.run(left,  "out", linked ? &right : nullptr);
        const auto outR = r.run(right, "out", linked ? &left  : nullptr);

        const std::size_t from = 12000;
        const auto level = [&](const std::vector<float>& s) { return ElementTestFixture::measureRms(s, from); };
        return std::pair{ level(outL) / level(left), level(outR) / level(right) };
    };

    const auto [unlinkedL, unlinkedR] = limited(false);
    const auto [linkedL, linkedR]     = limited(true);
    std::printf("  unlinked gains %.3f / %.3f, linked gains %.3f / %.3f\n",
                unlinkedL, unlinkedR, linkedL, linkedR);

    assert(unlinkedR > 0.99f && unlinkedL < 0.2f);
    assert(std::abs(linkedL - linkedR) < 0.01f);
    assert(linkedR < 0.2f);
}

void testAnUnconnectedLinkChangesNothing()
{
    ElementTestFixture a("Limiter", kRate), b("Limiter", kRate);
    const auto in = ElementTestFixture::generateSine(1000.0, kRate, 8192, 3.0f);
    const std::vector<float> silence(in.size(), 0.0f);
    assert(a.run(in) == b.run(in, "out", &silence));
}

}  // namespace

int main()
{
    testQuietSignalIsDelayedAndUntouched();
    testNothingExceedsTheCeiling();
    testItLimitsWithoutClipping();
    testGainRecoversAtTheReleaseAsked();
    testItRampsInsteadOfSnapping();
    testLinkKeepsTheImage();
    testAnUnconnectedLinkChangesNothing();

    std::printf("LimiterTest PASSED\n");
    return 0;
}

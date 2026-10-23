// tests/dsp/PitchShiftTest.cpp
//
// val:PitchShift. A pitch shifter is judged by what pitch comes out and by how
// much else comes out with it. A shift of zero must be a plain delay; a shifted
// sine must land on the pitch asked for, the right number of semitones away,
// with its own tone well above the artefacts of the heads jumping.

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

/// A second of the sine through the shifter, the last 16384 samples returned.
std::vector<float> shifted(double hz, float semitones)
{
    ElementTestFixture rig("PitchShift", kRate);
    rig.set("pitch", semitones);
    const auto out = rig.run(ElementTestFixture::generateSine(hz, kRate, 48000, 0.5f));
    return std::vector<float>(out.end() - kSize, out.end());
}

struct Peak { double hz; double level; };

/// The strongest partial, interpolated between bins, and its level.
Peak strongest(const std::vector<float>& signal)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, 14);
    std::size_t peak = 2;
    for (std::size_t b = 3; b + 2 < bins.size(); ++b)
        if (bins[b] > bins[peak])
            peak = b;

    const double a = std::log(std::max(bins[peak - 1], 1.0e-20f));
    const double b = std::log(std::max(bins[peak],     1.0e-20f));
    const double c = std::log(std::max(bins[peak + 1], 1.0e-20f));
    const double denominator = a - 2.0 * b + c;
    const double offset = denominator != 0.0 ? 0.5 * (a - c) / denominator : 0.0;
    return { (static_cast<double>(peak) + offset) * kRate / kSize, bins[peak] };
}

/// Level of the strongest bin more than `away` Hz from `centre`.
double strongestAway(const std::vector<float>& signal, double centre, double away)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, 14);
    double best = 0.0;
    for (std::size_t b = 1; b < bins.size(); ++b)
    {
        const double hz = static_cast<double>(b) * kRate / kSize;
        if (std::abs(hz - centre) > away)
            best = std::max(best, static_cast<double>(bins[b]));
    }
    return best;
}

void testZeroShiftIsADelay()
{
    ElementTestFixture rig("PitchShift", kRate);
    const int latency = rig.element->latencyInSamples();
    assert(latency > 100);

    const auto in  = ElementTestFixture::generateSine(777.0, kRate, 8192, 0.5f);
    const auto out = rig.run(in);
    for (std::size_t i = static_cast<std::size_t>(latency) + 4; i < in.size(); ++i)
        assert(std::abs(out[i] - in[i - static_cast<std::size_t>(latency)]) < 1.0e-5f);
}

void testItLandsOnThePitchAsked()
{
    for (const float semitones : { -12.0f, -5.0f, 3.0f, 7.0f, 12.0f })
    {
        const double expected = 1000.0 * std::pow(2.0, semitones / 12.0);
        const auto p = strongest(shifted(1000.0, semitones));
        const double cents = 1200.0 * std::log2(p.hz / expected);
        std::printf("  %+.0f semitones: %.1f Hz (expected %.1f), %.1f cents out\n",
                    semitones, p.hz, expected, cents);
        assert(std::abs(cents) < 10.0);
    }
}

void testTheToneStandsAboveTheArtefacts()
{
    for (const float semitones : { -7.0f, 5.0f })
    {
        const double expected = 1000.0 * std::pow(2.0, semitones / 12.0);
        const auto signal = shifted(1000.0, semitones);
        const auto p = strongest(signal);
        const double rest = strongestAway(signal, p.hz, 150.0);
        const double db = 20.0 * std::log10(p.level / std::max(rest, 1.0e-12));
        std::printf("  %+.0f semitones (%.0f Hz): tone is %.1f dB above anything 150 Hz away\n",
                    semitones, expected, db);
        assert(db > 25.0);
    }
}

void testMixBlendsWithoutAComb()
{
    // Shift of zero at half mix: the dry and wet are the same delayed signal, so
    // the level is that of the input and not a cancelled or doubled one.
    ElementTestFixture rig("PitchShift", kRate);
    rig.set("mix", 0.5f);
    const auto out = rig.run(ElementTestFixture::generateSine(2500.0, kRate, 16384, 0.5f));
    const std::vector<float> tail(out.end() - 8192, out.end());
    const double level = ElementTestFixture::measureRms(tail) / (0.5 / std::sqrt(2.0));
    std::printf("  half mix at zero shift: level %.4f\n", level);
    assert(std::abs(level - 1.0) < 0.01);
}

}  // namespace

int main()
{
    testZeroShiftIsADelay();
    testItLandsOnThePitchAsked();
    testTheToneStandsAboveTheArtefacts();
    testMixBlendsWithoutAComb();

    std::printf("PitchShiftTest PASSED\n");
    return 0;
}

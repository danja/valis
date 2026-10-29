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

/// The share of a shifted chord's power that is not in its three partials: the sidebands
/// that heads jumping out of step with each other put around every tone.
double chordSpill(bool align, float semitones)
{
    ElementTestFixture rig("PitchShift", kRate);
    rig.set("pitch", semitones);
    rig.set("align", align ? 1.0f : 0.0f);

    const double ratio = std::pow(2.0, semitones / 12.0);
    std::vector<float> chord(static_cast<std::size_t>(kRate));
    for (const double hz : { 220.0, 277.18, 329.63 })
    {
        const auto tone = ElementTestFixture::generateSine(hz, kRate, static_cast<int>(kRate), 0.15f);
        for (std::size_t i = 0; i < chord.size(); ++i)
            chord[i] += tone[i];
    }

    const auto out = rig.run(chord);
    const auto bins = ElementTestFixture::computeSpectrum(std::vector<float>(out.end() - kSize, out.end()), 14);

    double inside = 0.0, outside = 0.0;
    for (std::size_t b = 1; b < bins.size(); ++b)
    {
        const double hz = static_cast<double>(b) * kRate / kSize;
        const double power = static_cast<double>(bins[b]) * bins[b];
        bool near = false;
        for (const double f : { 220.0, 277.18, 329.63 })
            near = near || std::abs(hz - f * ratio) < 12.0;
        (near ? inside : outside) += power;
    }
    return outside / (inside + outside);
}

void testAlignSteadiesAChord()
{
    for (const float semitones : { 3.0f, -5.0f })
    {
        const double plain = chordSpill(false, semitones);
        const double aligned = chordSpill(true, semitones);
        std::printf("  chord shifted %+.0f: %.1f%% of the power is off its partials plain, %.1f%% aligned\n",
                    semitones, 100.0 * plain, 100.0 * aligned);
        assert(aligned < plain);
    }
}

void testAlignKeepsASinglePitch()
{
    // Alignment must not cost the single voice its pitch.
    ElementTestFixture rig("PitchShift", kRate);
    rig.set("pitch", 5.0f);
    rig.set("align", 1.0f);
    const auto out = rig.run(ElementTestFixture::generateSine(1000.0, kRate, 48000, 0.5f));
    const auto p = strongest(std::vector<float>(out.end() - kSize, out.end()));
    const double cents = 1200.0 * std::log2(p.hz / (1000.0 * std::pow(2.0, 5.0 / 12.0)));
    std::printf("  aligned +5: %.1f cents out\n", cents);
    assert(std::abs(cents) < 15.0);

    // And a zero shift is still an exact delay with it on.
    ElementTestFixture zero("PitchShift", kRate);
    zero.set("align", 1.0f);
    const auto in = ElementTestFixture::generateSine(777.0, kRate, 8192, 0.5f);
    const auto delayed = zero.run(in);
    const auto latency = static_cast<std::size_t>(zero.element->latencyInSamples());
    for (std::size_t i = latency + 4; i < in.size(); ++i)
        assert(std::abs(delayed[i] - in[i - latency]) < 1.0e-5f);
}

}  // namespace

int main()
{
    testZeroShiftIsADelay();
    testItLandsOnThePitchAsked();
    testTheToneStandsAboveTheArtefacts();
    testMixBlendsWithoutAComb();
    testAlignSteadiesAChord();
    testAlignKeepsASinglePitch();

    std::printf("PitchShiftTest PASSED\n");
    return 0;
}

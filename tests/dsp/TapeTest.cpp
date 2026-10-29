// tests/dsp/TapeTest.cpp
//
// val:Tape. Each of its four stages is judged by the thing it does: harmonics
// from the saturation, a lift at 80 Hz from the head bump, pitch movement from
// wow and flutter, and noise from the hiss. With everything off it is a delay.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

void clean(ElementTestFixture& rig)
{
    rig.set("drive", 0.0f);
    rig.set("bump", 0.0f);
    rig.set("wow", 0.0f);
    rig.set("flutter", 0.0f);
    rig.set("hf", 20000.0f);
    rig.set("hiss", -120.0f);
}

float levelAt(const std::vector<float>& signal, double hz, int order)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, order);
    const auto centre = static_cast<std::size_t>(std::lround(hz * (1 << order) / kRate));
    float best = 0.0f;
    for (std::size_t b = centre - 1; b <= centre + 1; ++b)
        best = std::max(best, bins[b]);
    return best;
}

std::vector<float> lastSamples(const std::vector<float>& v, std::size_t n)
{
    return std::vector<float>(v.end() - static_cast<std::ptrdiff_t>(n), v.end());
}

void testCleanTapeIsADelay()
{
    ElementTestFixture rig("Tape", kRate);
    clean(rig);
    const int latency = rig.element->latencyInSamples();
    assert(latency == 144);

    const auto in = ElementTestFixture::generateSine(500.0, kRate, 16384, 0.5f);
    const auto out = rig.run(in);
    // The only difference is the 20 kHz one-pole, which at 500 Hz is a small phase lag.
    const auto a = lastSamples(out, 8192);
    const double level = ElementTestFixture::measureRms(a) / (0.5 / std::sqrt(2.0));
    std::printf("  clean level %.4f\n", level);
    assert(std::abs(level - 1.0) < 0.01);
}

void testSaturationAddsHarmonicsOnlyWhenDriven()
{
    const auto thirdOverFundamental = [](float drive)
    {
        ElementTestFixture rig("Tape", kRate);
        clean(rig);
        rig.set("drive", drive);
        const double f = kRate * 1024 / 16384;   // exactly on a bin
        const auto out = rig.run(ElementTestFixture::generateSine(f, kRate, 16384, 0.5f));
        const auto tail = lastSamples(out, 8192);
        return 20.0 * std::log10(levelAt(tail, 3 * f, 13) / levelAt(tail, f, 13));
    };

    const double none = thirdOverFundamental(0.0f), driven = thirdOverFundamental(18.0f);
    std::printf("  third harmonic: %.1f dB at drive 0, %.1f dB at drive 18\n", none, driven);
    assert(none < -50.0);
    assert(driven > -30.0);
}

void testBumpLiftsTheLowEnd()
{
    const auto gainAt = [](double hz, float bump)
    {
        ElementTestFixture rig("Tape", kRate);
        clean(rig);
        rig.set("bump", bump);
        const auto out = rig.run(ElementTestFixture::generateSine(hz, kRate, 32768, 0.1f));
        return ElementTestFixture::measureRms(lastSamples(out, 16384)) / (0.1 / std::sqrt(2.0));
    };

    const double low = 20.0 * std::log10(gainAt(80.0, 6.0f)), high = 20.0 * std::log10(gainAt(2000.0, 6.0f));
    std::printf("  bump 6 dB: %.2f dB at 80 Hz, %.2f dB at 2 kHz\n", low, high);
    assert(std::abs(low - 6.0) < 0.5);
    assert(std::abs(high) < 0.3);
}

/// How much the period of a sine moves, as the spread of zero-crossing
/// intervals as a fraction of the mean.
double pitchMovement(float wow, float flutter)
{
    ElementTestFixture rig("Tape", kRate);
    clean(rig);
    rig.set("wow", wow);
    rig.set("flutter", flutter);

    const auto out = rig.run(ElementTestFixture::generateSine(1000.0, kRate, 96000, 0.5f));
    std::vector<double> crossings;
    for (std::size_t i = 4800; i + 1 < out.size(); ++i)
        if (out[i] <= 0.0f && out[i + 1] > 0.0f)
            crossings.push_back(static_cast<double>(i) + out[i] / (out[i] - out[i + 1]));

    double lowest = 1.0e9, highest = 0.0;
    for (std::size_t i = 1; i < crossings.size(); ++i)
    {
        const double period = crossings[i] - crossings[i - 1];
        lowest = std::min(lowest, period);
        highest = std::max(highest, period);
    }
    return (highest - lowest) / (kRate / 1000.0);
}

void testWowAndFlutterMoveThePitch()
{
    const double still = pitchMovement(0.0f, 0.0f);
    const double wow = pitchMovement(1.0f, 0.0f);
    const double flutter = pitchMovement(0.0f, 1.0f);
    std::printf("  period spread: still %.5f, wow %.5f, flutter %.5f\n", still, wow, flutter);
    assert(still < 0.002);
    assert(wow > 0.004);
    assert(flutter > 0.002);
}

void testHissIsNoiseAtTheStatedLevel()
{
    ElementTestFixture rig("Tape", kRate);
    clean(rig);
    rig.set("hiss", -40.0f);

    const auto out = rig.run(std::vector<float>(32768, 0.0f));
    const double rms = ElementTestFixture::measureRms(lastSamples(out, 16384));
    const double db = 20.0 * std::log10(rms);
    std::printf("  hiss -40 dB reads %.1f dB rms\n", db);
    assert(db < -40.0 && db > -50.0);   // uniform noise of peak 1 has an rms of -4.8 dB

    ElementTestFixture off("Tape", kRate);
    clean(off);
    const auto quiet = off.run(std::vector<float>(32768, 0.0f));
    assert(ElementTestFixture::measureRms(lastSamples(quiet, 16384)) < 1.0e-5f);
}

void testSeedsRepeat()
{
    const auto render = [](float seed)
    {
        ElementTestFixture rig("Tape", kRate);
        rig.set("seed", seed);
        return rig.run(ElementTestFixture::generateSine(440.0, kRate, 8192, 0.3f));
    };
    assert(render(3.0f) == render(3.0f));
    assert(render(3.0f) != render(4.0f));
}

}  // namespace

int main()
{
    testCleanTapeIsADelay();
    testSaturationAddsHarmonicsOnlyWhenDriven();
    testBumpLiftsTheLowEnd();
    testWowAndFlutterMoveThePitch();
    testHissIsNoiseAtTheStatedLevel();
    testSeedsRepeat();

    std::printf("TapeTest PASSED\n");
    return 0;
}

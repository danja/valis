// tests/dsp/BrassTest.cpp
//
// val:Brass. A lip-reed model is only worth having if it behaves like a brass instrument, so these measure what
// makes it one: it speaks only when blown, it plays the pitch it is given over the range of a trumpet, it gets
// brighter as it is blown harder (the one thing a filter on a saw wave does not do), and the notes of its range are
// of comparable loudness, so a melody does not need a volume curve.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 4096;

/// Runs the element until the oscillation has built, then returns a settled block.
std::vector<float> settled(ElementTestFixture& rig, int blocks = 24)
{
    std::vector<float> out;
    const std::vector<float> silence(static_cast<std::size_t>(kBlock), 0.0f);
    for (int i = 0; i < blocks; ++i)
        out = rig.run(silence);
    return out;
}

double dominantFrequency(const std::vector<float>& signal, double near)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, 12);
    const double binWidth = kRate / kBlock;

    std::size_t peak = static_cast<std::size_t>(near * 0.7 / binWidth);
    const std::size_t last = static_cast<std::size_t>(near * 1.4 / binWidth);
    for (std::size_t b = peak; b <= last && b + 1 < bins.size(); ++b)
        if (bins[b] > bins[peak])
            peak = b;

    const double a = std::log(std::max(bins[peak - 1], 1.0e-20f));
    const double b = std::log(std::max(bins[peak],     1.0e-20f));
    const double c = std::log(std::max(bins[peak + 1], 1.0e-20f));
    const double denominator = a - 2.0 * b + c;
    const double offset = denominator != 0.0 ? 0.5 * (a - c) / denominator : 0.0;
    return (static_cast<double>(peak) + offset) * binWidth;
}

/// The spectral centroid in Hz below 8 kHz, a measure of brightness.
double centroid(const std::vector<float>& signal)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, 12);
    const double binWidth = kRate / kBlock;
    double weighted = 0.0, total = 0.0;
    for (std::size_t b = 1; b < bins.size() && b * binWidth < 8000.0; ++b)
    {
        weighted += bins[b] * b * binWidth;
        total += bins[b];
    }
    return total > 0.0 ? weighted / total : 0.0;
}

ElementTestFixture player(double frequency, float pressure)
{
    ElementTestFixture rig("Brass", kRate);
    rig.set("frequency", static_cast<float>(frequency));
    rig.set("pressure", pressure);
    return rig;
}

/// An unblown horn is silent, and so is one blown below the pressure the note needs.
void testSilentUntilBlown()
{
    auto rig = player(440.0, 0.0f);
    assert(ElementTestFixture::measurePeak(settled(rig)) == 0.0f);

    auto weak = player(440.0, 0.04f);
    const float peak = ElementTestFixture::measurePeak(settled(weak));
    std::printf("  brass: peak barely blown %.6f\n", peak);
    assert(peak < 0.02f);
}

/// Over the range of a trumpet, G3 to G6, it speaks at the pitch it is asked for. The tuning is a measured table,
/// so the worst error is a number to keep an eye on rather than zero.
void testSpeaksAtTheGivenPitch()
{
    double worst = 0.0;
    float quietest = 1.0e9f, loudest = 0.0f;

    for (const double frequency : {196.0, 262.0, 349.0, 440.0, 587.0, 784.0, 1047.0, 1319.0, 1568.0})
    {
        auto rig = player(frequency, 0.8f);
        const auto out = settled(rig);

        const double cents = 1200.0 * std::log2(dominantFrequency(out, frequency) / frequency);
        worst = std::max(worst, std::abs(cents));

        const float rms = ElementTestFixture::measureRms(out);
        assert(rms > 0.02f);
        assert(ElementTestFixture::measurePeak(out) < 2.0f);
        quietest = std::min(quietest, rms);
        loudest  = std::max(loudest, rms);

        for (float sample : out)
            assert(std::isfinite(sample));
    }

    const double spread = 20.0 * std::log10(loudest / quietest);
    std::printf("  brass: worst tuning error %.1f cents, level spread %.1f dB over G3 to G6\n", worst, spread);
    assert(worst < 30.0);
    assert(spread < 14.0);
}

/// A trumpet gets brighter as it is blown harder. That is physics here, not an envelope on a filter.
void testBrighterWhenBlownHarder()
{
    for (const double frequency : {262.0, 440.0, 698.0})
    {
        auto soft = player(frequency, 0.25f);
        auto loud = player(frequency, 1.0f);
        const double softCentroid = centroid(settled(soft));
        const double loudCentroid = centroid(settled(loud));
        std::printf("  brass: %.0f Hz centroid %.0f Hz soft, %.0f Hz loud\n", frequency, softCentroid, loudCentroid);
        assert(softCentroid > 0.0);
        assert(loudCentroid > softCentroid * 1.08);
    }
}

/// A trumpet has many harmonics: the fifth is not far below the fundamental at a normal level, which separates it
/// from a filtered saw's smooth fall and from a sine-like flute.
void testHasManyHarmonics()
{
    auto rig = player(440.0, 0.7f);
    const auto out = settled(rig);
    const auto bins = ElementTestFixture::computeSpectrum(out, 12);
    const double f0 = dominantFrequency(out, 440.0);
    const float first = ElementTestFixture::measureEnergyAt(bins, f0, kRate);
    const float fifth = ElementTestFixture::measureEnergyAt(bins, f0 * 5.0, kRate);
    const double drop = 10.0 * std::log10(std::max(fifth, 1.0e-12f) / std::max(first, 1.0e-12f));
    std::printf("  brass: fifth harmonic %.1f dB from the fundamental\n", drop);
    assert(drop > -30.0);
}

}  // namespace

int main()
{
    testSilentUntilBlown();
    testSpeaksAtTheGivenPitch();
    testBrighterWhenBlownHarder();
    testHasManyHarmonics();
    std::printf("BrassTest PASSED\n");
    return 0;
}

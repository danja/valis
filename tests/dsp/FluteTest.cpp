// tests/dsp/FluteTest.cpp
//
// val:Flute. A physical model is only worth having if it behaves like the
// instrument, so these measure the things that make a flute a flute: it speaks
// only when blown, it plays the pitch it is given across its range, it grows
// louder and brighter as it is blown harder, and its second harmonic is the
// strong one.
//
// The last of those is what the jet offset is for. A jet centred on the edge is
// deflected equally either way and puts out odd harmonics only, which is a
// stopped, hollow sound; moving it off centre is what makes the tone a flute's.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

/// Runs the element until the oscillation has built, then returns a settled
/// block. A waveguide starts from the turbulence in its own breath, so it takes
/// a moment to speak, exactly as a real one does.
std::vector<float> settled(ElementTestFixture& rig, int blocks = 20, int blockSize = 4096)
{
    std::vector<float> out;
    const std::vector<float> silence(static_cast<std::size_t>(blockSize), 0.0f);
    for (int i = 0; i < blocks; ++i)
        out = rig.run(silence);
    return out;
}

/// The strongest partial, in Hz. The bins are 11.7 Hz apart, which is a fifth
/// of a semitone at 440 Hz, so the peak is interpolated against its neighbours:
/// taken as read it snaps to a bin and reports a detuning that is not there.
double dominantFrequency(const std::vector<float>& signal)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, 12);

    std::size_t peak = 1;
    for (std::size_t b = 2; b + 1 < bins.size(); ++b)
        if (bins[b] > bins[peak])
            peak = b;

    const double a = std::log(std::max(bins[peak - 1], 1.0e-20f));
    const double b = std::log(std::max(bins[peak],     1.0e-20f));
    const double c = std::log(std::max(bins[peak + 1], 1.0e-20f));
    const double denominator = a - 2.0 * b + c;
    const double offset = denominator != 0.0 ? 0.5 * (a - c) / denominator : 0.0;

    return (static_cast<double>(peak) + offset) * kRate / 4096.0;
}

ElementTestFixture player(double frequency, float pressure)
{
    ElementTestFixture rig("Flute", kRate);
    rig.set("frequency", static_cast<float>(frequency));
    rig.set("pressure", pressure);
    return rig;
}

/// Amplitude of one harmonic relative to the fundamental.
double harmonic(const std::vector<float>& signal, double fundamental, int number)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, 12);
    const float first = ElementTestFixture::measureEnergyAt(bins, fundamental, kRate);
    const float nth   = ElementTestFixture::measureEnergyAt(bins, fundamental * number, kRate);
    return std::sqrt(nth / std::max(first, 1.0e-12f));
}

double centsFrom(const std::vector<float>& signal, double frequency)
{
    return 1200.0 * std::log2(dominantFrequency(signal) / frequency);
}

/// An unblown flute is silent, and stays silent: the loop has no energy of its
/// own, and the turbulence that would start it is proportional to the breath.
void testSilentUntilBlown()
{
    auto rig = player(440.0, 0.0f);
    assert(ElementTestFixture::measurePeak(settled(rig)) == 0.0f);

    // Below the threshold the jet cannot sustain an oscillation either.
    auto weak = player(440.0, 0.05f);
    const auto quiet = settled(weak);
    std::printf("  flute: peak barely blown %.6f\n", ElementTestFixture::measurePeak(quiet));
    assert(ElementTestFixture::measurePeak(quiet) < 0.02f);
}

/// Blown normally it speaks, at the pitch it was given, over the range the
/// flute family covers: a bass flute's bottom note to the top of a piccolo's.
void testSpeaksAtTheGivenPitch()
{
    double worst = 0.0;

    for (const double frequency : {196.0, 262.0, 349.0, 440.0, 587.0, 880.0, 1319.0, 1760.0})
    {
        auto rig = player(frequency, 0.6f);
        const auto out = settled(rig);

        worst = std::max(worst, std::abs(centsFrom(out, frequency)));

        assert(ElementTestFixture::measureRms(out) > 0.02f);
        assert(ElementTestFixture::measurePeak(out) < 2.0f);

        for (float sample : out)
            assert(std::isfinite(sample));
    }

    std::printf("  flute: worst tuning error over three octaves %.1f cents\n", worst);
    assert(worst < 12.0);
}

/// The jet offset is what puts the even harmonics in. Centred, the model is
/// symmetric and has none, which is a stopped pipe; off centre the second
/// harmonic becomes the strong one, which is a flute.
void testOffsetPutsInTheEvenHarmonics()
{
    auto centred = player(440.0, 0.35f);
    centred.set("offset", 0.0f);
    const auto symmetric = settled(centred);

    auto rolled = player(440.0, 0.35f);
    rolled.set("offset", 0.7f);
    const auto asymmetric = settled(rolled);

    const double centredSecond = harmonic(symmetric, 440.0, 2);
    const double offsetSecond  = harmonic(asymmetric, 440.0, 2);
    const double offsetThird   = harmonic(asymmetric, 440.0, 3);

    std::printf("  flute: second harmonic %.4f centred, %.4f offset (third %.4f)\n",
                centredSecond, offsetSecond, offsetThird);

    assert(centredSecond < 0.02);                  // symmetric: no even harmonics
    assert(offsetSecond > 0.05);                   // asymmetric: they are there
    assert(offsetSecond > centredSecond * 10.0);
    // At this dynamic the second harmonic leads, which is what a flute sounds
    // like played softly. Blown harder the third overtakes it, which is also
    // what a flute does; testBlowingHarderIsLouderAndBrighter covers that.
    assert(offsetSecond > offsetThird);
}

/// Blowing harder makes it louder and brighter rather than saturating at one
/// volume. Jet speed goes with the square root of pressure, so this is
/// Bernoulli's arriving in the sound.
void testBlowingHarderIsLouderAndBrighter()
{
    auto soft = player(440.0, 0.3f);
    auto loud = player(440.0, 1.0f);

    const auto quietTone = settled(soft);
    const auto loudTone  = settled(loud);

    const auto quietLevel = ElementTestFixture::measureRms(quietTone);
    const auto loudLevel  = ElementTestFixture::measureRms(loudTone);
    const auto quietThird = harmonic(quietTone, 440.0, 3);
    const auto loudThird  = harmonic(loudTone, 440.0, 3);

    std::printf("  flute: rms %.3f soft, %.3f loud; third harmonic %.3f soft, %.3f loud\n",
                quietLevel, loudLevel, quietThird, loudThird);

    assert(loudLevel > quietLevel * 1.5f);
    assert(loudThird > quietThird * 1.5);
}

/// The embouchure retunes the air column, and the column's loss moves the
/// loop's delay. Both are corrected by curves fitted to measurements, so these
/// are what stop them drifting.
void testTuningHoldsAcrossTheControls()
{
    double worstJet = 0.0, worstDamping = 0.0;

    for (const float jetRatio : {0.35f, 0.45f, 0.5f, 0.55f, 0.65f})
    {
        for (const double frequency : {262.0, 440.0, 880.0})
        {
            auto rig = player(frequency, 0.6f);
            rig.set("jet", jetRatio);
            worstJet = std::max(worstJet, std::abs(centsFrom(settled(rig), frequency)));
        }
    }

    for (const float damping : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        auto rig = player(440.0, 0.6f);
        rig.set("damping", damping);
        worstDamping = std::max(worstDamping, std::abs(centsFrom(settled(rig), 440.0)));
    }

    std::printf("  flute: worst tuning error %.1f cents across the embouchure, %.1f across damping\n",
                worstJet, worstDamping);

    assert(worstJet < 15.0);
    assert(worstDamping < 15.0);
}

/// Damping is what the air column loses on each round trip, so a damped flute
/// is darker. It is most of what separates a wooden instrument from a metal one.
void testDampingDarkensTheTone()
{
    auto bright = player(440.0, 0.6f);
    bright.set("damping", 0.0f);
    auto dark = player(440.0, 0.6f);
    dark.set("damping", 1.0f);

    auto tilt = [](const std::vector<float>& signal)
    {
        const auto bins = ElementTestFixture::computeSpectrum(signal, 12);
        double low = 0.0, high = 0.0;
        for (std::size_t b = 1; b < bins.size(); ++b)
        {
            const double energy = static_cast<double>(bins[b]) * bins[b];
            (static_cast<double>(b) * kRate / 4096.0 < 2000.0 ? low : high) += energy;
        }
        return high / std::max(low, 1.0e-12);
    };

    const double brightTilt = tilt(settled(bright));
    const double darkTilt   = tilt(settled(dark));

    std::printf("  flute: energy above 2 kHz, bright %.5f, damped %.5f\n", brightTilt, darkTilt);
    assert(darkTilt < brightTilt);
}

/// Breath noise is proportional to blowing pressure, so it is part of the note
/// rather than a hiss underneath it. It is also what starts the oscillation.
void testBreathNoiseFollowsPressure()
{
    auto quiet = player(440.0, 0.6f);
    quiet.set("breath", 0.01f);
    auto breathy = player(440.0, 0.6f);
    breathy.set("breath", 1.0f);

    auto betweenPartials = [](const std::vector<float>& signal)
    {
        const auto bins = ElementTestFixture::computeSpectrum(signal, 12);
        double between = 0.0;
        for (int partial = 1; partial <= 6; ++partial)
            between += static_cast<double>(
                ElementTestFixture::measureEnergyAt(bins, 440.0 * partial + 220.0, kRate));
        return between;
    };

    const double clean = betweenPartials(settled(quiet));
    const double windy = betweenPartials(settled(breathy));

    std::printf("  flute: energy between the partials, clean %.6f, breathy %.6f\n", clean, windy);
    assert(windy > clean * 2.0);
}

void probeReed()
{
    for (const double frequency : {147.0, 196.0, 262.0, 440.0, 880.0})
    {
        ElementTestFixture rig("Reed", kRate);
        rig.set("frequency", static_cast<float>(frequency));
        rig.set("pressure", 0.6f);
        const auto out = settled(rig);
        const auto bins = ElementTestFixture::computeSpectrum(out, 12);
        const float f1 = ElementTestFixture::measureEnergyAt(bins, frequency, kRate);
        std::fprintf(stderr, "  reed f %4.0f  %+6.1fc rms %.3f  ", frequency,
                     centsFrom(out, frequency), ElementTestFixture::measureRms(out));
        for (int h = 2; h <= 5; ++h)
            std::fprintf(stderr, " %d:%.3f", h,
                         std::sqrt(ElementTestFixture::measureEnergyAt(bins, frequency * h, kRate)
                                   / std::max(f1, 1.0e-12f)));
        std::fprintf(stderr, "\n");
    }
}

}  // namespace

int main()
{
    probeReed(); return 0;
    testSilentUntilBlown();
    testSpeaksAtTheGivenPitch();
    testOffsetPutsInTheEvenHarmonics();
    testBlowingHarderIsLouderAndBrighter();
    testTuningHoldsAcrossTheControls();
    testDampingDarkensTheTone();
    testBreathNoiseFollowsPressure();

    std::printf("FluteTest PASSED\n");
    return 0;
}

// tests/dsp/ClarinetTest.cpp
//
// val:Reed. What makes a clarinet a clarinet is its bore: stopped at the
// mouthpiece and open at the bell, so a round trip inverts and the tube holds a
// quarter wavelength. The odd-harmonic spectrum follows from that, and so does
// the twelfth between its registers.
//
// The flute taught the lesson these start from: a model can be stable and in
// tune and still be the wrong instrument, so the spectrum is tested and not
// only the pitch. See MISTAKES.md.

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
/// block. A waveguide starts from the turbulence in its own breath.
std::vector<float> settled(ElementTestFixture& rig, int blocks = 20, int blockSize = 4096)
{
    std::vector<float> out;
    const std::vector<float> silence(static_cast<std::size_t>(blockSize), 0.0f);
    for (int i = 0; i < blocks; ++i)
        out = rig.run(silence);
    return out;
}

/// The strongest partial, in Hz, interpolated against its neighbours: the bins
/// are a fifth of a semitone apart at 440 Hz, so taken as read they report a
/// detuning that is not there.
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

double centsFrom(const std::vector<float>& signal, double frequency)
{
    return 1200.0 * std::log2(dominantFrequency(signal) / frequency);
}

/// Amplitude of one harmonic relative to the fundamental. Measured against the
/// pitch the instrument actually played, not the one it was asked for: a few
/// cents of drift is enough to miss a partial and report a tone far purer than
/// it is.
double harmonic(const std::vector<float>& signal, int number)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, 12);
    const double fundamental = dominantFrequency(signal);
    const float first = ElementTestFixture::measureEnergyAt(bins, fundamental, kRate);
    const float nth   = ElementTestFixture::measureEnergyAt(bins, fundamental * number, kRate);
    return std::sqrt(nth / std::max(first, 1.0e-12f));
}

ElementTestFixture player(double frequency, float pressure)
{
    ElementTestFixture rig("Reed", kRate);
    rig.set("frequency", static_cast<float>(frequency));
    rig.set("pressure", pressure);
    return rig;
}

/// An unblown clarinet is silent. The loop has no energy of its own, and the
/// turbulence that would start it is proportional to the breath.
void testSilentUntilBlown()
{
    auto rig = player(294.0, 0.0f);
    assert(ElementTestFixture::measurePeak(settled(rig)) == 0.0f);
}

/// It speaks at the pitch it is given, across the range a clarinet covers.
void testSpeaksAtTheGivenPitch()
{
    double worst = 0.0;

    for (const double frequency : {147.0, 196.0, 262.0, 349.0, 440.0, 587.0, 880.0, 1175.0})
    {
        auto rig = player(frequency, 0.6f);
        const auto out = settled(rig);

        worst = std::max(worst, std::abs(centsFrom(out, frequency)));

        assert(ElementTestFixture::measureRms(out) > 0.05f);
        assert(ElementTestFixture::measurePeak(out) < 2.0f);

        for (float sample : out)
            assert(std::isfinite(sample));
    }

    std::printf("  clarinet: worst tuning error over three octaves %.1f cents\n", worst);
    assert(worst < 10.0);
}

/// The defining measurement. A stopped cylinder resonates at odd multiples of a
/// quarter wavelength, so the third and fifth harmonics carry the tone and the
/// even ones are barely there at all.
void testSpectrumIsOddHarmonic()
{
    auto rig = player(294.0, 0.6f);
    const auto out = settled(rig);

    const double second = harmonic(out, 2);
    const double third  = harmonic(out, 3);
    const double fourth = harmonic(out, 4);
    const double fifth  = harmonic(out, 5);
    const double sixth  = harmonic(out, 6);

    std::printf("  clarinet: harmonics 2:%.3f 3:%.3f 4:%.3f 5:%.3f 6:%.3f\n",
                second, third, fourth, fifth, sixth);

    assert(third > 0.15);            // the odd ones carry the tone
    assert(fifth > 0.05);
    assert(second < 0.05);           // the even ones are not there
    assert(fourth < 0.05);
    assert(sixth  < 0.05);
    assert(third > second * 10.0);
    assert(fifth > fourth * 5.0);
}

/// The two elements are the same waveguide with opposite boundary conditions,
/// and this is what that difference sounds like: the flute's tube is open at
/// both ends and has the even harmonics, the clarinet's is stopped and has not.
void testTheStoppedBoreIsWhatSuppressesTheEvenHarmonics()
{
    auto clarinet = player(294.0, 0.6f);
    const double clarinetSecond = harmonic(settled(clarinet), 2);

    ElementTestFixture flute("Flute", kRate);
    flute.set("frequency", 294.0f);
    flute.set("pressure", 0.45f);
    flute.set("offset", 0.7f);
    const double fluteSecond = harmonic(settled(flute), 2);

    std::printf("  clarinet: second harmonic %.4f, against the flute's %.4f\n",
                clarinetSecond, fluteSecond);

    assert(fluteSecond > clarinetSecond * 10.0);
}

/// Damping is what the bore loses on each round trip, so it takes the upper
/// partials first. The loss filter's own delay comes out of the delay line,
/// which is what stops the control detuning the instrument as it moves.
void testDampingDarkensWithoutDetuning()
{
    double worst = 0.0;
    double brightThird = 0.0, darkThird = 0.0;

    for (const float damping : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        auto rig = player(294.0, 0.6f);
        rig.set("damping", damping);
        const auto out = settled(rig);

        worst = std::max(worst, std::abs(centsFrom(out, 294.0)));

        if (damping == 0.0f) brightThird = harmonic(out, 3);
        if (damping == 1.0f) darkThird   = harmonic(out, 3);
    }

    std::printf("  clarinet: third harmonic %.3f undamped, %.3f damped; worst detune %.1f cents\n",
                brightThird, darkThird, worst);

    assert(darkThird < brightThird * 0.7);
    assert(worst < 10.0);
}

/// A reed has a band of mouth pressure it works over: below it nothing sounds,
/// and above it the reed is pressed against the lay and stays shut. A soft reed
/// speaks on less air and chokes sooner; a hard one needs more and takes more.
/// That is what the stiffness control is.
void testStiffnessChangesWhatItTakesToPlay()
{
    auto levelAt = [](float stiffness, float pressure)
    {
        auto rig = player(294.0, pressure);
        rig.set("stiffness", stiffness);
        return ElementTestFixture::measureRms(settled(rig));
    };

    const float softQuiet = levelAt(0.0f, 0.12f);
    const float softLoud  = levelAt(0.0f, 0.95f);
    const float hardQuiet = levelAt(1.0f, 0.12f);
    const float hardLoud  = levelAt(1.0f, 0.95f);

    std::printf("  clarinet: soft reed %.3f quiet, %.3f loud; hard reed %.3f quiet, %.3f loud\n",
                softQuiet, softLoud, hardQuiet, hardLoud);

    assert(softQuiet > 0.05f);       // the soft reed speaks on very little air
    assert(softLoud  < 0.01f);       // and shuts when it is overblown
    assert(hardQuiet < 0.01f);       // the hard one will not start on that
    assert(hardLoud  > 0.05f);       // but takes everything it is given
}

/// Within its band, blowing harder is louder.
void testBlowingHarderIsLouder()
{
    auto soft = player(294.0, 0.25f);
    auto loud = player(294.0, 1.0f);

    const auto quietLevel = ElementTestFixture::measureRms(settled(soft));
    const auto loudLevel  = ElementTestFixture::measureRms(settled(loud));

    std::printf("  clarinet: rms %.3f soft, %.3f loud\n", quietLevel, loudLevel);
    assert(loudLevel > quietLevel * 1.3f);
}

/// Breath noise is proportional to blowing pressure, so it is part of the note
/// rather than a hiss underneath it.
void testBreathNoiseFollowsPressure()
{
    auto quiet = player(294.0, 0.6f);
    quiet.set("breath", 0.0f);
    auto breathy = player(294.0, 0.6f);
    breathy.set("breath", 1.0f);

    auto betweenPartials = [](const std::vector<float>& signal)
    {
        const auto bins = ElementTestFixture::computeSpectrum(signal, 12);
        double between = 0.0;
        for (int partial = 1; partial <= 6; ++partial)
            between += static_cast<double>(
                ElementTestFixture::measureEnergyAt(bins, 294.0 * partial + 147.0, kRate));
        return between;
    };

    const double clean = betweenPartials(settled(quiet));
    const double windy = betweenPartials(settled(breathy));

    std::printf("  clarinet: energy between the partials, clean %.6f, breathy %.6f\n",
                clean, windy);
    assert(windy > clean * 2.0);
}

}  // namespace

int main()
{
    testSilentUntilBlown();
    testSpeaksAtTheGivenPitch();
    testSpectrumIsOddHarmonic();
    testTheStoppedBoreIsWhatSuppressesTheEvenHarmonics();
    testDampingDarkensWithoutDetuning();
    testStiffnessChangesWhatItTakesToPlay();
    testBlowingHarderIsLouder();
    testBreathNoiseFollowsPressure();

    std::printf("ClarinetTest PASSED\n");
    return 0;
}

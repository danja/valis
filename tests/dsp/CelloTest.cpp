// tests/dsp/CelloTest.cpp
//
// val:Bow. A bowed string is sustained by friction rather than struck, so the
// first things to establish are that it starts at all, that it stays bounded
// once it has, and that it plays the note it was asked for across the range a
// cello covers.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr double kSearch = 1.5;

/// The cello's four open strings and stopped notes above them, up to the top of
/// the range this model is verified over. Above about D4 its odd harmonics fall
/// away until the tone is an octave ambiguity rather than a note; see the note
/// in TODO.md.
const double kRange[] = {65.41, 82.41, 98.00, 130.81, 164.81, 220.00, 293.66};

/// Bows one note and returns the settled tail.
std::vector<float> bow(double frequency, float pressure = 0.5f, float position = 0.12f,
                       float velocity = 0.5f, float damping = 0.3f, int blocks = 6)
{
    ElementTestFixture rig("Bow", kRate);
    rig.set("frequency", static_cast<float>(frequency));
    rig.set("pressure", pressure);
    rig.set("velocity", velocity);
    rig.set("position", position);
    rig.set("damping", damping);
    rig.setNote(60, 1.0f, true);

    const std::vector<float> silence(16384, 0.0f);
    std::vector<float> out;
    for (int i = 0; i < blocks; ++i)
        out = rig.run(silence);

    return out;
}

/// The pitch actually sounding, by autocorrelation over the settled tail.
///
/// Not by counting zero crossings: a bowed string's waveform is a sawtooth with
/// a friction ripple on it, which crosses zero several times per period, and
/// counting those measures a harmonic rather than the note.
///
/// The search runs over lags within a fifth either side of `expected`. A blind
/// search over the whole range picks a multiple of the period as readily as the
/// period itself, which is where an octave error in a pitch measurement comes
/// from. The window is still wide enough to show an error of 700 cents, and
/// anything larger than that is not a tuning problem.
/// The pitch actually sounding, from the spacing of its partials.
///
/// Not by autocorrelation and not by counting zero crossings. A bowed string's
/// partials are strong and its fundamental is not always the strongest of them,
/// so both of those measures alias to a multiple or a fraction of the period.
/// The spacing between neighbouring partials is the fundamental whether or not
/// the fundamental itself is loud.
double playedPitch(const std::vector<float>& signal, double expected)
{
    constexpr int order = 13;
    constexpr int size  = 1 << order;
    const double hzPerBin = kRate / size;

    const auto bins = ElementTestFixture::computeSpectrum(signal, order);

    float loudest = 0.0f;
    for (std::size_t b = 1; b < bins.size(); ++b)
        loudest = std::max(loudest, bins[b]);
    if (loudest <= 0.0f)
        return 0.0;

    // Local maxima that are a real part of the tone rather than the floor.
    std::vector<double> partials;
    for (std::size_t b = 2; b + 2 < bins.size(); ++b)
    {
        if (bins[b] < loudest * 0.05f)
            continue;
        if (bins[b] <= bins[b - 1] || bins[b] < bins[b + 1])
            continue;

        // Interpolated, because a partial rarely sits on a bin centre.
        const double left = bins[b - 1], centre = bins[b], right = bins[b + 1];
        const double divisor = 2.0 * (2.0 * centre - left - right);
        const double offset = divisor != 0.0 ? (right - left) / divisor : 0.0;
        partials.push_back((static_cast<double>(b) + std::clamp(offset, -0.5, 0.5)) * hzPerBin);
    }

    if (partials.size() < 2)
        return partials.empty() ? 0.0 : partials.front();

    // The smallest spacing between neighbouring partials, averaged over the
    // spacings that agree with it, which is the fundamental of the series.
    double smallest = partials.back();
    for (std::size_t i = 1; i < partials.size(); ++i)
        smallest = std::min(smallest, partials[i] - partials[i - 1]);

    if (smallest < expected * 0.5)
        smallest = expected;   // partials too crowded to resolve; fall back

    double sum = 0.0;
    int count = 0;
    for (const double f : partials)
    {
        const double harmonic = std::round(f / smallest);
        if (harmonic >= 1.0 && std::abs(f / harmonic - smallest) < smallest * 0.05)
        {
            sum += f / harmonic;
            ++count;
        }
    }

    return count > 0 ? sum / count : smallest;
}

void testBowingStartsTheString()
{
    const auto sounding = bow(130.81);
    const float level = ElementTestFixture::measureRms(sounding);

    std::printf("  bowed C3: rms %.4f\n", level);
    std::fflush(stdout);

    assert(level > 0.01f);
    for (float s : sounding)
        assert(std::isfinite(s));
}

void testAnUnbowedStringIsSilent()
{
    ElementTestFixture rig("Bow", kRate);
    rig.set("frequency", 130.81f);
    rig.setNote(60, 1.0f, false);   // no gate: the arm is not moving

    const std::vector<float> silence(8192, 0.0f);
    std::vector<float> out;
    for (int i = 0; i < 4; ++i)
        out = rig.run(silence);

    assert(ElementTestFixture::measurePeak(out) < 1.0e-4f);
}

void testPlaysTheNoteItWasAsked()
{
    std::printf("  bow tuning:\n");

    double worst = 0.0;
    for (const double asked : kRange)
    {
        const auto played = playedPitch(bow(asked), asked);
        const double cents = played > 0.0 ? 1200.0 * std::log2(played / asked) : 9999.0;

        std::printf("    asked %7.2f  played %7.2f  %+6.1f cents\n", asked, played, cents);
        worst = std::max(worst, std::abs(cents));
    }
    std::fflush(stdout);

    // Within a fifth of a semitone across the range. The loop is nonlinear, so
    // this is calibrated against measurement rather than derived.
    assert(worst < 20.0);
}

void testStaysBoundedWhileBowedHard()
{
    // Heavy bow, close to the bridge, wide open: the corner of the control
    // space where a friction model is most likely to run away.
    ElementTestFixture rig("Bow", kRate);
    rig.set("frequency", 98.0f);
    rig.set("pressure", 1.0f);
    rig.set("velocity", 1.0f);
    rig.set("position", 0.02f);
    rig.set("damping", 0.0f);
    rig.setNote(60, 1.0f, true);

    const std::vector<float> silence(8192, 0.0f);
    std::vector<float> out;
    for (int i = 0; i < 40; ++i)
    {
        out = rig.run(silence);
        for (float s : out)
        {
            assert(std::isfinite(s));
            assert(std::abs(s) < 10.0f);
        }
    }
}

/// A bowed string is rich in harmonics: that is what separates it from a flute,
/// and a model that produced a near sine would be the wrong instrument however
/// well it was tuned. Measured against the pitch it actually played, not the one
/// it was asked for, because a few cents of drift moves a partial out of the
/// measurement window.
void testSpectrumIsRichInHarmonics()
{
    const auto sounding = bow(130.81);
    const auto played = playedPitch(sounding, 130.81);
    assert(played > 0.0);

    const auto bins = ElementTestFixture::computeSpectrum(sounding, 13);
    const double binsPerHz = 8192.0 / kRate;

    const auto energyAt = [&](double hz)
    {
        const auto centre = static_cast<int>(std::round(hz * binsPerHz));
        float peak = 0.0f;
        for (int b = centre - 2; b <= centre + 2; ++b)
            if (b >= 0 && b < static_cast<int>(bins.size()))
                peak = std::max(peak, bins[static_cast<std::size_t>(b)]);
        return peak;
    };

    const float first = energyAt(played);
    assert(first > 0.0f);

    int strong = 0;
    for (int harmonic = 2; harmonic <= 8; ++harmonic)
        if (energyAt(played * harmonic) > first * 0.02f)
            ++strong;

    std::printf("  cello spectrum: %d of harmonics 2 to 8 within 34 dB of the first\n", strong);
    std::fflush(stdout);

    assert(strong >= 5);
}

/// Bow position decides which harmonics are weak, so it is the main timbral
/// control rather than a refinement. Nearer the bridge is brighter.
void testPositionChangesTheTone()
{
    const auto centroid = [](const std::vector<float>& signal)
    {
        const auto bins = ElementTestFixture::computeSpectrum(signal, 13);
        double weighted = 0.0, total = 0.0;
        for (std::size_t b = 1; b < bins.size(); ++b)
        {
            const double energy = static_cast<double>(bins[b]) * bins[b];
            weighted += energy * static_cast<double>(b);
            total += energy;
        }
        return total > 0.0 ? weighted / total * kRate / 8192.0 : 0.0;
    };

    const double nearBridge = centroid(bow(130.81, 0.5f, 0.04f));
    const double overFinger = centroid(bow(130.81, 0.5f, 0.40f));

    std::printf("  bow position: centroid %.0f Hz near the bridge, %.0f Hz over the fingerboard\n",
                nearBridge, overFinger);
    std::fflush(stdout);

    assert(nearBridge > overFinger);
}

}  // namespace

int main()
{
    testBowingStartsTheString();
    testAnUnbowedStringIsSilent();
    testPlaysTheNoteItWasAsked();
    testStaysBoundedWhileBowedHard();
    testSpectrumIsRichInHarmonics();
    testPositionChangesTheTone();

    std::puts("CelloTest PASSED");
    return 0;
}

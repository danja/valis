// tests/dsp/StiffStringTest.cpp
//
// val:StiffString is a comb filter with a damping filter and an allpass chain
// in its loop. Both add delay, so a string that only counts samples plays flat,
// and by more the higher the note: the loop is a fixed few tens of microseconds
// too long, which is a bigger fraction of a shorter period.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate  = 48000.0;
constexpr int    kOrder = 14;
constexpr int    kFrame = 1 << kOrder;

/// Plucks the string with an impulse and measures its fundamental from a settled
/// frame, interpolating the spectral peak between bins (a bin here is 2.9 Hz,
/// which is 23 cents at 220 Hz, so an uninterpolated peak would hide the error).
double pitchOf(double frequency, float damping, float dispersion)
{
    ElementTestFixture rig("StiffString", kRate);
    rig.set("frequency",  static_cast<float>(frequency));
    rig.set("feedback",   0.99f);
    rig.set("damping",    damping);
    rig.set("dispersion", dispersion);

    std::vector<float> pluck(static_cast<std::size_t>(kFrame), 0.0f);
    pluck[0] = 1.0f;
    rig.run(pluck);
    const auto settled = rig.run(std::vector<float>(static_cast<std::size_t>(kFrame), 0.0f));

    const auto bins = ElementTestFixture::computeSpectrum(settled, kOrder);

    // Search near the expected pitch only, so an upper partial cannot win.
    const double binWidth = kRate / kFrame;
    const std::size_t lo = static_cast<std::size_t>(frequency * 0.8 / binWidth);
    const std::size_t hi = static_cast<std::size_t>(frequency * 1.25 / binWidth);
    std::size_t peak = lo;
    for (std::size_t b = lo; b <= hi; ++b)
        if (bins[b] > bins[peak])
            peak = b;

    const double a = std::log(std::max(bins[peak - 1], 1.0e-20f));
    const double b = std::log(std::max(bins[peak],     1.0e-20f));
    const double c = std::log(std::max(bins[peak + 1], 1.0e-20f));
    const double d = a - 2.0 * b + c;
    const double offset = d != 0.0 ? 0.5 * (a - c) / d : 0.0;

    return (static_cast<double>(peak) + offset) * binWidth;
}

double centsFrom(double measured, double expected)
{
    return 1200.0 * std::log2(measured / expected);
}

/// Before the loop was corrected the string was 25 cents flat at E3, 58 at E4
/// and 104 at E5. It must now hold across the range at the default damping and
/// dispersion.
void testInTuneAcrossTheRange()
{
    double worst = 0.0;
    for (const double f : {82.4, 130.8, 164.8, 220.0, 329.6, 440.0, 659.3, 880.0})
    {
        const double cents = centsFrom(pitchOf(f, 0.1f, 0.1f), f);
        std::printf("  stiff string: %7.1f Hz is %+.1f cents\n", f, cents);
        worst = std::max(worst, std::abs(cents));
    }
    assert(worst < 6.0);
}

/// Damping and dispersion each move the loop's delay, so the compensation has to
/// follow them.
void testInTuneAcrossDampingAndDispersion()
{
    double worst = 0.0;
    for (const float damping : {0.0f, 0.1f, 0.3f, 0.6f})
        for (const float dispersion : {0.0f, 0.1f, 0.3f})
            for (const double f : {196.0, 329.6, 659.3})
                worst = std::max(worst, std::abs(centsFrom(pitchOf(f, damping, dispersion), f)));

    std::printf("  stiff string: worst error across damping and dispersion %.1f cents\n", worst);
    assert(worst < 10.0);
}

/// The feedback port runs to 0.9999. Past that value the loop must still decay, stay finite, and ring
/// longer than at the old limit of 0.99.
void testALongRingStaysStable()
{
    const auto energyAfterSeconds = [](float feedback)
    {
        ElementTestFixture rig("StiffString", kRate);
        rig.set("frequency", 196.0f);
        rig.set("feedback", feedback);
        rig.set("damping", 0.1f);
        rig.set("dispersion", 0.1f);

        std::vector<float> pluck(static_cast<std::size_t>(kRate), 0.0f);
        pluck[0] = 1.0f;
        rig.run(pluck);

        double peak = 0.0;
        for (int second = 1; second < 6; ++second)
            for (const float v : rig.run(std::vector<float>(static_cast<std::size_t>(kRate), 0.0f)))
            {
                assert(std::isfinite(v));
                assert(std::abs(v) < 2.0f);
                if (second == 5)
                    peak = std::max(peak, static_cast<double>(std::abs(v)));
            }
        return peak;
    };

    const double old = energyAfterSeconds(0.99f);
    const double longRing = energyAfterSeconds(0.9999f);
    std::printf("  stiff string: level five seconds in, %.2g at 0.99 and %.2g at 0.9999\n", old, longRing);
    assert(longRing > old * 10.0);
    assert(energyAfterSeconds(1.5f) <= longRing * 1.001);   // above the port's range it is held at 0.9999
}

/// Magnitude of `x` at `frequency` over [from, to) seconds, by a Hann-windowed single-bin transform.
double magnitudeAt(const std::vector<float>& x, double rate, double frequency, double from, double to)
{
    const auto a = static_cast<std::size_t>(from * rate), b = static_cast<std::size_t>(to * rate);
    double re = 0.0, im = 0.0;
    for (std::size_t i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i - a) / static_cast<double>(b - a));
        const double ph = 2.0 * M_PI * frequency * static_cast<double>(i) / rate;
        re += x[i] * w * std::cos(ph);
        im -= x[i] * w * std::sin(ph);
    }
    return std::sqrt(re * re + im * im);
}

/// The frequency near `guess` with the most energy over [0.2 s, 1.2 s).
double peakNear(const std::vector<float>& x, double rate, double guess)
{
    double best = guess, bestMag = 0.0;
    for (double f = guess * 0.98; f <= guess * 1.04; f += guess * 0.0005)
    {
        const double m = magnitudeAt(x, rate, f, 0.2, 1.2);
        if (m > bestMag) { bestMag = m; best = f; }
    }
    return best;
}

struct RateReport { double stretch; double decayDbPerSecond; };

RateReport playAt(double rate)
{
    ElementTestFixture rig("StiffString", rate);
    rig.set("frequency", 220.0f);
    rig.set("feedback", 0.99f);
    rig.set("damping", 0.3f);
    rig.set("dispersion", 1.0f);

    std::vector<float> impulse(static_cast<std::size_t>(2.4 * rate), 0.0f);
    impulse[0] = 1.0f;
    const auto x = rig.run(impulse);

    const double f1 = peakNear(x, rate, 220.0);
    const double f8 = peakNear(x, rate, 8.0 * f1 * 1.02);
    const double ring = 20.0 * std::log10(magnitudeAt(x, rate, f1, 0.3, 0.9) / magnitudeAt(x, rate, f1, 1.5, 2.1));
    return {f8 / (8.0 * f1), ring / 1.2};
}

/// The loop filters were fixed per sample, so their corner frequencies moved with the host rate: at 192 kHz
/// the loss pole and the dispersion allpass sat at a quarter of their 48 kHz frequencies. A string must keep
/// its inharmonic stretch (set by the allpass) and its ring time (set by the loss) at any rate.
void testRingsTheSameAtOtherRates()
{
    const RateReport reference = playAt(48000.0);
    assert(reference.stretch > 1.005);   // the dispersion is audible, so the comparison means something
    for (const double rate : {44100.0, 88200.0, 96000.0, 192000.0})
    {
        const RateReport r = playAt(rate);
        std::printf("  stiff string at %6.0f Hz: stretch %.4f (%.4f at 48 kHz), ring %.2f dB/s (%.2f)\n",
                    rate, r.stretch, reference.stretch, r.decayDbPerSecond, reference.decayDbPerSecond);
        assert(std::abs(r.stretch / reference.stretch - 1.0) < 0.003);
        assert(std::abs(r.decayDbPerSecond - reference.decayDbPerSecond) < 0.5);
    }
}

}  // namespace

int main()
{
    testInTuneAcrossTheRange();
    testInTuneAcrossDampingAndDispersion();
    testALongRingStaysStable();
    testRingsTheSameAtOtherRates();
    std::printf("StiffStringTest passed\n");
    return 0;
}

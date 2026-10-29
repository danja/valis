// tests/dsp/WavetableTest.cpp
//
// val:Wavetable. The shapes at the ends of the first set have known spectra, so
// the table is checked against them. Then the property the element exists for:
// a bright frame played high does not alias.

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

std::vector<float> play(double hz, float position, int set = 0, int samples = 48000)
{
    ElementTestFixture rig("Wavetable", kRate);
    rig.set("frequency", static_cast<float>(hz));
    rig.set("position", position);
    rig.set("set", static_cast<float>(set));
    return rig.run(std::vector<float>(static_cast<std::size_t>(samples), 0.0f));
}

/// Amplitude at one frequency by correlation over the second half of the signal.
double amplitudeAt(const std::vector<float>& signal, double hz)
{
    double re = 0.0, im = 0.0;
    const std::size_t from = signal.size() / 2;
    for (std::size_t i = from; i < signal.size(); ++i)
    {
        const double phase = 2.0 * kPi * hz * static_cast<double>(i) / kRate;
        re += signal[i] * std::cos(phase);
        im += signal[i] * std::sin(phase);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(signal.size() - from);
}

double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

void testPositionZeroIsASine()
{
    const auto out = play(480.0, 0.0f);
    const double h1 = amplitudeAt(out, 480.0);
    std::printf("  sine: fundamental %.3f, third harmonic %.1f dB\n", h1, db(amplitudeAt(out, 1440.0) / h1));
    assert(std::abs(h1 - 1.0) < 0.01);
    assert(db(amplitudeAt(out, 1440.0) / h1) < -70.0);
}

void testPositionOneIsASquare()
{
    const auto out = play(480.0, 1.0f);
    const double h1 = amplitudeAt(out, 480.0);
    const double h3 = db(amplitudeAt(out, 1440.0) / h1);
    const double h2 = db(amplitudeAt(out, 960.0) / h1);
    std::printf("  square: third %.2f dB (expected %.2f), second %.1f dB\n", h3, db(1.0 / 3.0), h2);
    assert(std::abs(h3 - db(1.0 / 3.0)) < 0.3);
    assert(h2 < -60.0);
}

void testTwoThirdsIsASaw()
{
    const auto out = play(480.0, 2.0f / 3.0f);
    const double h1 = amplitudeAt(out, 480.0);
    for (int k = 2; k <= 5; ++k)
    {
        const double got = db(amplitudeAt(out, 480.0 * k) / h1);
        std::printf("  saw: harmonic %d at %.2f dB (expected %.2f)\n", k, got, db(1.0 / k));
        assert(std::abs(got - db(1.0 / k)) < 0.3);
    }
}

void testItDoesNotAlias()
{
    // 7 kHz saw: its fourth harmonic, at 28 kHz, would fold to 20 kHz, where a naive
    // table has it at -12 dB. Levels step by octaves, so the third harmonic at 21 kHz,
    // which is legitimate, is left out with it: dimmer, but never false.
    const auto out = play(7000.0, 2.0f / 3.0f);
    const double h1 = amplitudeAt(out, 7000.0);
    const double h2 = db(amplitudeAt(out, 14000.0) / h1);
    const double folded = db(amplitudeAt(out, 20000.0) / h1);
    std::printf("  7 kHz saw: second harmonic %.2f dB (expected %.2f), alias at 20 kHz %.1f dB\n",
                h2, db(0.5), folded);
    assert(std::abs(h2 - db(0.5)) < 0.3);
    assert(folded < -60.0);
}

void testPitchIsExact()
{
    for (const double hz : { 55.0, 261.63, 1234.5, 9000.0 })
    {
        const auto out = play(hz, 0.0f, 0, 96000);
        // The sine frame: a single partial, so the best correlation is at the pitch.
        const double at = amplitudeAt(out, hz);
        const double off = amplitudeAt(out, hz * 1.001);
        std::printf("  %.2f Hz: %.4f at pitch, %.4f at +1.7 cents\n", hz, at, off);
        assert(at > 0.99);
        assert(off < at);
    }
}

void testEverySetIsFiniteAndNormalised()
{
    for (int set = 0; set < 3; ++set)
        for (const float position : { 0.0f, 0.3f, 0.6f, 1.0f })
            for (const double hz : { 40.0, 440.0, 5000.0, 15000.0 })
            {
                const auto out = play(hz, position, set, 9600);
                float peak = 0.0f;
                for (float s : out)
                {
                    assert(std::isfinite(s));
                    peak = std::max(peak, std::abs(s));
                }
                // Every table peaks at 1; a blend of two frames of different shape can
                // peak a little under it, but not by much.
                assert(peak <= 1.0001f);
                assert(peak > 0.4f);
            }
}

void testSweepingThePositionIsContinuous()
{
    // A buzzy frame has steep edges of its own, so the yardstick is the steepest step
    // of the two frames played steadily. A position that stepped at a block boundary
    // would show a jump beyond both.
    const auto steepest = [](const std::vector<float>& s)
    {
        float largest = 0.0f;
        for (std::size_t i = 1; i < s.size(); ++i)
            largest = std::max(largest, std::abs(s[i] - s[i - 1]));
        return largest;
    };

    float steady = 0.0f;
    for (int i = 0; i <= 10; ++i)
        steady = std::max(steady, steepest(play(220.0, static_cast<float>(i) / 10.0f, 1, 8192)));

    ElementTestFixture rig("Wavetable", kRate);
    rig.set("frequency", 220.0f);
    rig.set("set", 1.0f);

    std::vector<float> all;
    for (int block = 0; block < 40; ++block)
    {
        rig.set("position", block % 2 == 0 ? 0.0f : 1.0f);
        const auto part = rig.run(std::vector<float>(512, 0.0f));
        all.insert(all.end(), part.begin(), part.end());
    }

    const float swept = steepest(all);
    std::printf("  steepest step: %.3f steady, %.3f with the position flung about\n", steady, swept);
    assert(swept < 1.15f * steady);
}

}  // namespace

/// Power in harmonics `from` to `to` of a saw at `hz`, against the fundamental's.
double upperHarmonics(double hz, int from, int to)
{
    const auto out = play(hz, 2.0f / 3.0f);
    const double fundamental = amplitudeAt(out, hz);
    double power = 0.0;
    for (int k = from; k <= to; ++k)
    {
        const double a = amplitudeAt(out, hz * k);
        power += a * a;
    }
    return power / (fundamental * fundamental);
}

void testLevelsCrossfadeInsteadOfStepping()
{
    // Level 3 holds 128 harmonics and level 4 holds 64. Level 3 stops being safe at
    // 187.5 Hz. Harmonics 70 to 128 are what separates them: they must be fading out
    // through the half octave before that point, and gone by it, rather than
    // present until the last moment and then dropped at once.
    double previous = 1.0e9;
    for (const double hz : { 130.0, 150.0, 165.0, 178.0, 185.0, 187.0, 188.0, 195.0 })
    {
        const double power = upperHarmonics(hz, 70, 120);
        std::printf("  %.0f Hz: harmonics 70 to 120 are %.1f dB of the fundamental\n", hz, db(std::sqrt(power)));
        assert(power <= previous * 1.05 || power < 1.0e-6);   // below -60 dB is measurement noise
        previous = power;
    }

    // Either side of the old switching point the sound is the same to a fraction of a decibel.
    const double before = upperHarmonics(187.0, 70, 120), after = upperHarmonics(188.0, 70, 120);
    assert(before < 0.01 * upperHarmonics(150.0, 70, 120));
    assert(before < 1.0e-5 && after < 1.0e-5);
}

int main()
{
    testPositionZeroIsASine();
    testPositionOneIsASquare();
    testTwoThirdsIsASaw();
    testItDoesNotAlias();
    testPitchIsExact();
    testEverySetIsFiniteAndNormalised();
    testSweepingThePositionIsContinuous();
    testLevelsCrossfadeInsteadOfStepping();

    std::printf("WavetableTest PASSED\n");
    return 0;
}

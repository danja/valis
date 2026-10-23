// tests/dsp/StateVariableDriveTest.cpp
//
// val:StateVariable's drive. At 1 the filter must stay exactly linear, so every
// circuit written before the control existed sounds as it did. Above 1 a quiet
// signal must be left alone while a loud one is compressed and made odd-harmonic,
// and a filter pushed to self-oscillation must hold a level rather than grow.

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

std::vector<float> run(float drive, float resonance, double hz, float amplitude, std::string_view port = "lp")
{
    ElementTestFixture rig("StateVariable", kRate);
    rig.set("cutoff", 1000.0f);
    rig.set("resonance", resonance);
    rig.set("drive", drive);
    return rig.run(ElementTestFixture::generateSine(hz, kRate, 48000, amplitude), port);
}

void testUnityDriveIsLinear()
{
    // Linear means scaling the input scales the output by exactly the same factor.
    const auto small = run(1.0f, 4.0f, 1000.0, 0.1f);
    const auto large = run(1.0f, 4.0f, 1000.0, 1.0f);
    for (std::size_t i = 0; i < small.size(); ++i)
        assert(std::abs(large[i] - 10.0f * small[i]) < 1.0e-4f * (1.0f + std::abs(large[i])));
}

void testAQuietSignalIsLeftAlone()
{
    const auto plain = run(1.0f, 2.0f, 700.0, 0.001f);
    const auto driven = run(10.0f, 2.0f, 700.0, 0.001f);
    const double a = amplitudeAt(plain, 24000, 700.0);
    const double b = amplitudeAt(driven, 24000, 700.0);
    std::printf("  quiet tone: %.6f plain, %.6f driven\n", a, b);
    assert(std::abs(a - b) < 0.01 * a);
}

void testALoudSignalIsCompressedAndGainsOddHarmonics()
{
    const auto plain = run(1.0f, 2.0f, 700.0, 1.0f);
    const auto driven = run(8.0f, 2.0f, 700.0, 1.0f);

    const double a = amplitudeAt(plain, 24000, 700.0);
    const double b = amplitudeAt(driven, 24000, 700.0);
    const double third  = amplitudeAt(driven, 24000, 2100.0) / b;
    const double second = amplitudeAt(driven, 24000, 1400.0) / b;
    const double thirdPlain = amplitudeAt(plain, 24000, 2100.0) / a;
    std::printf("  loud tone: %.3f plain, %.3f driven; third harmonic %.1f dB (plain %.1f), second %.1f dB\n",
                a, b, 20.0 * std::log10(third), 20.0 * std::log10(thirdPlain), 20.0 * std::log10(second));

    assert(b < 0.6 * a);
    assert(third > 30.0 * thirdPlain);
    assert(second < 0.2 * third);        // odd harmonics, from a symmetric curve
}

void testResonanceHoldsALevel()
{
    // A hard ping into a filter at its limit of resonance. Undriven it rings at
    // a level set by the input; driven, the ring is held under about 1 / drive.
    ElementTestFixture rig("StateVariable", kRate);
    rig.set("cutoff", 1000.0f);
    rig.set("resonance", 20.0f);
    rig.set("drive", 4.0f);

    auto ping = ElementTestFixture::generateImpulse(48000, 50.0f);
    const auto out = rig.run(ping, "bp");
    for (float s : out)
        assert(std::isfinite(s));
    float peak = 0.0f;
    for (float s : out)
        peak = std::max(peak, std::abs(s));
    std::printf("  50x impulse at Q 20, drive 4: peak %.3f\n", peak);
    assert(peak < 1.0f);
}

}  // namespace

int main()
{
    testUnityDriveIsLinear();
    testAQuietSignalIsLeftAlone();
    testALoudSignalIsCompressedAndGainsOddHarmonics();
    testResonanceHoldsALevel();

    std::printf("StateVariableDriveTest PASSED\n");
    return 0;
}

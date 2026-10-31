// tests/dsp/FreeModalBankTest.cpp
//
// val:FreeModalBank: every mode is a damped sine of a stated frequency, ring time and level, so a unit
// impulse must play back exactly the numbers written in val:modes.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi   = 3.14159265358979323846;

std::vector<float> impulse(int n)
{
    std::vector<float> x(static_cast<std::size_t>(n), 0.0f);
    x[0] = 1.0f;
    return x;
}

/// Strength of one frequency in a signal, by correlation with a complex sinusoid.
double strengthAt(const std::vector<float>& x, double hz)
{
    double re = 0.0, im = 0.0;
    for (std::size_t n = 0; n < x.size(); ++n)
    {
        const double phase = 2.0 * kPi * hz * static_cast<double>(n) / kRate;
        re += x[n] * std::cos(phase);
        im += x[n] * std::sin(phase);
    }
    return std::sqrt(re * re + im * im) / static_cast<double>(x.size());
}

bool optionFails(const char* modes)
{
    ElementTestFixture rig("FreeModalBank", kRate);
    std::string error;
    const bool ok = rig.element->setOption("modes", modes, error);
    if (! ok)
        assert(! error.empty());
    return ! ok;
}

void testAnImpulsePlaysBackTheNumbersWritten()
{
    ElementTestFixture rig("FreeModalBank", kRate);
    std::string error;
    const bool ok = rig.element->setOption("modes", "440:1:0.5", error);
    assert(ok);

    const auto out = rig.run(impulse(4096));
    const double omega = 2.0 * kPi * 440.0 / kRate;
    const double r = std::pow(0.001, 1.0 / (kRate * 1.0));
    double worst = 0.0;
    for (int n = 0; n < 4096; ++n)
    {
        const double expected = 0.5 * std::pow(r, n) * std::sin((n + 1) * omega);
        worst = std::max(worst, std::abs(out[static_cast<std::size_t>(n)] - expected));
    }
    std::printf("  free modal bank: impulse response differs from the formula by at most %.2e\n", worst);
    assert(worst < 5.0e-4);   // single precision: the resonator drifts a little in phase over 4096 samples
}

void testARingTimeIsTheTimeToFallSixtyDecibels()
{
    ElementTestFixture rig("FreeModalBank", kRate);
    std::string error;
    const bool ok = rig.element->setOption("modes", "1000:0.5:1", error);
    assert(ok);

    const auto out = rig.run(impulse(static_cast<int>(kRate)));
    const auto peakIn = [&](double from, double to)
    {
        float peak = 0.0f;
        for (auto i = static_cast<std::size_t>(from * kRate); i < static_cast<std::size_t>(to * kRate); ++i)
            peak = std::max(peak, std::abs(out[i]));
        return static_cast<double>(peak);
    };

    // At 0.3 s of a 0.5 s ring the level is 10^(-3 * 0.3 / 0.5) of the start.
    const double expected = std::pow(10.0, -3.0 * 0.3 / 0.5);
    const double measured = peakIn(0.30, 0.31);
    std::printf("  free modal bank: level at 0.3 s is %.4f, expected %.4f\n", measured, expected);
    assert(measured > expected * 0.9 && measured < expected * 1.05);
}

void testEachModeSoundsAtItsOwnFrequency()
{
    ElementTestFixture rig("FreeModalBank", kRate);
    std::string error;
    const bool ok = rig.element->setOption("modes", "440:2:1 1234:2:1", error);
    assert(ok);

    const auto out = rig.run(impulse(24000));
    const double low = strengthAt(out, 440.0), high = strengthAt(out, 1234.0), between = strengthAt(out, 800.0);
    std::printf("  free modal bank: 440 Hz %.3e, 1234 Hz %.3e, 800 Hz %.3e\n", low, high, between);
    assert(low > between * 20.0);
    assert(high > between * 20.0);
}

void testTuneMovesEveryMode()
{
    ElementTestFixture rig("FreeModalBank", kRate);
    std::string error;
    const bool ok = rig.element->setOption("modes", "440:2:1", error);
    assert(ok);
    rig.set("tune", 2.0f);

    const auto out = rig.run(impulse(24000));
    assert(strengthAt(out, 880.0) > strengthAt(out, 440.0) * 20.0);

    // A mode tuned past the top of the band is dropped, not folded back below it.
    ElementTestFixture high("FreeModalBank", kRate);
    const bool okHigh = high.element->setOption("modes", "15000:1:1", error);
    assert(okHigh);
    high.set("tune", 4.0f);
    float peak = 0.0f;
    for (const float s : high.run(impulse(4800)))
        peak = std::max(peak, std::abs(s));
    assert(peak == 0.0f);
}

void testDecayScalesTheRing()
{
    const auto levelAt = [](float scale)
    {
        ElementTestFixture rig("FreeModalBank", kRate);
        std::string error;
        const bool ok = rig.element->setOption("modes", "1000:0.5:1", error);
        assert(ok);
        rig.set("decay", scale);
        const auto out = rig.run(impulse(static_cast<int>(kRate)));
        float peak = 0.0f;
        for (std::size_t i = static_cast<std::size_t>(0.3 * kRate); i < static_cast<std::size_t>(0.31 * kRate); ++i)
            peak = std::max(peak, std::abs(out[i]));
        return peak;
    };
    assert(levelAt(2.0f) > levelAt(1.0f) * 3.0f);
    assert(levelAt(0.5f) < levelAt(1.0f) * 0.3f);
}

void testSilenceWithoutExcitationOrModes()
{
    ElementTestFixture rig("FreeModalBank", kRate);
    for (const float s : rig.run(impulse(2048)))
        assert(s == 0.0f);   // no val:modes: nothing to ring

    std::string error;
    const bool ok = rig.element->setOption("modes", "440:1:1", error);
    assert(ok);
    rig.reset();
    for (const float s : rig.run(std::vector<float>(2048, 0.0f)))
        assert(s == 0.0f);   // modes, but never struck
}

void testBadModesAreRefusedWithAReason()
{
    assert(optionFails(""));
    assert(optionFails("440"));              // no ring time
    assert(optionFails("440:1:1:1"));        // too many parts
    assert(optionFails("abc:1:1"));
    assert(optionFails("440:1:"));
    assert(optionFails("10:1:1"));           // below 20 Hz
    assert(optionFails("30000:1:1"));
    assert(optionFails("440:0:1"));          // no ring time at all
    assert(optionFails("440:100:1"));
    assert(optionFails("440:1:-1"));
    assert(optionFails("nan:1:1"));

    std::string many;
    for (int i = 0; i < 65; ++i)
        many += std::to_string(100 + i * 10) + ":1:1 ";
    assert(optionFails(many.c_str()));

    // The gain may be left out, and sixty-four is allowed.
    ElementTestFixture rig("FreeModalBank", kRate);
    std::string error;
    const bool ok = rig.element->setOption("modes", "440:1", error);
    assert(ok);
    many.resize(many.size() - std::string("264:1:1 ").size());
    ElementTestFixture full("FreeModalBank", kRate);
    const bool okFull = full.element->setOption("modes", many, error);
    assert(okFull);

    // An option it does not know is not a failure.
    const bool unknown = rig.element->setOption("nothing", "x", error);
    assert(unknown);
}

}  // namespace

int main()
{
    testAnImpulsePlaysBackTheNumbersWritten();
    testARingTimeIsTheTimeToFallSixtyDecibels();
    testEachModeSoundsAtItsOwnFrequency();
    testTuneMovesEveryMode();
    testDecayScalesTheRing();
    testSilenceWithoutExcitationOrModes();
    testBadModesAreRefusedWithAReason();
    std::puts("FreeModalBankTest passed");
    return 0;
}

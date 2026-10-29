// tests/dsp/ReverbTest.cpp
//
// val:Reverb. A reverb is judged by its tail, so these measure the tail: it falls
// at the rate val:decay asks for, it cannot grow at the longest setting, damping
// takes the highs first, and a smaller room fills in sooner. Mix 0 must be the
// input untouched.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate  = 48000.0;
constexpr int    kBlock = 4096;

/// The response to a unit impulse, `seconds` long, run a block at a time.
std::vector<float> impulseResponse(ElementTestFixture& rig, double seconds)
{
    const int total = static_cast<int>(seconds * kRate);
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(total));

    for (int done = 0; done < total; done += kBlock)
    {
        const int n = std::min(kBlock, total - done);
        auto block = rig.run(done == 0 ? ElementTestFixture::generateImpulse(n)
                                       : std::vector<float>(static_cast<std::size_t>(n), 0.0f));
        out.insert(out.end(), block.begin(), block.end());
    }
    return out;
}

/// Energy between two times, in decibels.
double windowDb(const std::vector<float>& signal, double from, double to)
{
    double sum = 0.0;
    const auto a = static_cast<std::size_t>(from * kRate);
    const auto b = std::min(static_cast<std::size_t>(to * kRate), signal.size());
    for (std::size_t i = a; i < b; ++i)
        sum += static_cast<double>(signal[i]) * signal[i];
    return 10.0 * std::log10(std::max(sum / static_cast<double>(b - a), 1.0e-30));
}

void testMixZeroIsDry()
{
    ElementTestFixture rig("Reverb", kRate);
    rig.set("mix", 0.0f);

    const auto in  = ElementTestFixture::generateSine(440.0, kRate, kBlock, 0.5f);
    const auto out = rig.run(in);
    for (std::size_t i = 0; i < in.size(); ++i)
        assert(out[i] == in[i]);
}

void testTailFallsAtTheDecayAsked()
{
    // Damping 0 leaves the lines at their set gain, so 60 dB in `decay` seconds
    // means 30 dB in half of it.
    for (const float decay : { 1.0f, 2.0f })
    {
        ElementTestFixture rig("Reverb", kRate);
        rig.set("mix", 1.0f);
        rig.set("damping", 0.0f);
        rig.set("decay", decay);

        const auto tail = impulseResponse(rig, decay + 0.5);
        const double early = windowDb(tail, 0.2, 0.3);
        const double late  = windowDb(tail, 0.2 + 0.5 * decay, 0.3 + 0.5 * decay);
        const double fall  = early - late;
        const double expected = 60.0 * (0.5 * decay) / decay;

        std::printf("  decay %.1f s: fell %.1f dB in half the time (expected %.1f)\n",
                    decay, fall, expected);
        assert(std::abs(fall - expected) < 4.0);
    }
}

void testDecayIsExactAtAnySize()
{
    // At a size that makes the delays fractional, the tail used to fall faster than
    // asked, because interpolating between samples is a lowpass.
    for (const float size : { 1.0f, 1.37f, 0.63f })
    {
        ElementTestFixture rig("Reverb", kRate);
        rig.set("mix", 1.0f);
        rig.set("damping", 0.0f);
        rig.set("decay", 2.0f);
        rig.set("size", size);

        const auto tail = impulseResponse(rig, 2.5);
        const double fall = windowDb(tail, 0.4, 0.5) - windowDb(tail, 1.4, 1.5);
        std::printf("  size %.2f: fell %.1f dB in 1 s (expected 30)\n", size, fall);
        assert(std::abs(fall - 30.0) < 3.0);
    }
}

void testLongestSettingCannotGrow()
{
    ElementTestFixture rig("Reverb", kRate);
    rig.set("mix", 1.0f);
    rig.set("damping", 0.0f);
    rig.set("decay", 20.0f);
    rig.set("size", 2.0f);

    const auto tail = impulseResponse(rig, 12.0);
    for (float s : tail)
        assert(std::isfinite(s));

    assert(windowDb(tail, 11.0, 12.0) < windowDb(tail, 0.5, 1.5));
    assert(ElementTestFixture::measurePeak(tail) < 2.0f);
}

void testDampingTakesTheHighsFirst()
{
    // A decaying tail through a lowpass loses brightness as it goes: the same
    // moment in the tail is quieter with more damping.
    ElementTestFixture open("Reverb", kRate);
    open.set("mix", 1.0f);
    open.set("damping", 0.0f);
    open.set("decay", 3.0f);

    ElementTestFixture damped("Reverb", kRate);
    damped.set("mix", 1.0f);
    damped.set("damping", 1.0f);
    damped.set("decay", 3.0f);

    const auto a = impulseResponse(open, 1.5);
    const auto b = impulseResponse(damped, 1.5);

    std::printf("  tail at 1 s: open %.1f dB, damped %.1f dB\n",
                windowDb(a, 1.0, 1.2), windowDb(b, 1.0, 1.2));
    assert(windowDb(b, 1.0, 1.2) < windowDb(a, 1.0, 1.2) - 3.0);
}

void testSmallerRoomFillsInSooner()
{
    ElementTestFixture small("Reverb", kRate);
    small.set("mix", 1.0f);
    small.set("size", 0.2f);

    ElementTestFixture large("Reverb", kRate);
    large.set("mix", 1.0f);
    large.set("size", 2.0f);

    const auto a = impulseResponse(small, 0.2);
    const auto b = impulseResponse(large, 0.2);

    // The shortest line of the large room is about 45 ms, so its first 20 ms
    // hold no echo yet; the small room's are already sounding.
    std::printf("  first 20 ms: small %.1f dB, large %.1f dB\n",
                windowDb(a, 0.0, 0.02), windowDb(b, 0.0, 0.02));
    assert(windowDb(a, 0.0, 0.02) > windowDb(b, 0.0, 0.02));
}

void testResizingStaysFinite()
{
    ElementTestFixture rig("Reverb", kRate);
    rig.set("mix", 1.0f);
    rig.set("decay", 10.0f);

    auto in = ElementTestFixture::generateSine(220.0, kRate, kBlock, 0.5f);
    float size = 0.1f;
    for (int block = 0; block < 40; ++block)
    {
        size = size < 1.0f ? 2.0f : 0.1f;
        rig.set("size", size);
        for (float s : rig.run(in))
            assert(std::isfinite(s) && std::abs(s) < 20.0f);
    }
}

void testStereoTailsAreAlikeButNotCopies()
{
    // The same impulse through two identical rigs, one read at each output.
    const auto tail = [](std::string_view port)
    {
        ElementTestFixture rig("Reverb", kRate);
        rig.set("mix", 1.0f);
        rig.set("damping", 0.0f);
        rig.set("decay", 2.0f);

        const int total = static_cast<int>(1.5 * kRate);
        std::vector<float> out;
        for (int done = 0; done < total; done += kBlock)
        {
            const int n = std::min(kBlock, total - done);
            const auto block = rig.run(done == 0 ? ElementTestFixture::generateImpulse(n)
                                                 : std::vector<float>(static_cast<std::size_t>(n), 0.0f),
                                       port);
            out.insert(out.end(), block.begin(), block.end());
        }
        return out;
    };

    const auto l = tail("out");
    const auto r = tail("right");

    // Same decay: the two tails fall together.
    const double fallL = windowDb(l, 0.2, 0.3) - windowDb(l, 1.0, 1.1);
    const double fallR = windowDb(r, 0.2, 0.3) - windowDb(r, 1.0, 1.1);

    // Not copies: the correlation between them is small.
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (std::size_t i = static_cast<std::size_t>(0.1 * kRate); i < l.size(); ++i)
    {
        lr += static_cast<double>(l[i]) * r[i];
        ll += static_cast<double>(l[i]) * l[i];
        rr += static_cast<double>(r[i]) * r[i];
    }
    const double correlation = lr / std::sqrt(ll * rr);
    std::printf("  left fell %.1f dB, right %.1f dB, correlation %.3f\n", fallL, fallR, correlation);
    assert(std::abs(fallL - fallR) < 3.0);
    assert(std::abs(correlation) < 0.2);
}

}  // namespace

int main()
{
    testMixZeroIsDry();
    testTailFallsAtTheDecayAsked();
    testDecayIsExactAtAnySize();
    testLongestSettingCannotGrow();
    testDampingTakesTheHighsFirst();
    testSmallerRoomFillsInSooner();
    testResizingStaysFinite();
    testStereoTailsAreAlikeButNotCopies();

    std::printf("ReverbTest PASSED\n");
    return 0;
}

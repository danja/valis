// tests/engine/DistortionBenchTest.cpp
//
// circuits/distortion-bench.ttl (scripts/elements-demo/distortion-bench.py), played through the engine: silent until a
// note; every curve is a different sound at about the same level; and each curve has the harmonic signature its
// description claims, measured on a sine so that the source adds no harmonics of its own. The symmetric curves give
// odd harmonics only, the diode and the triode give even ones, and the hard clip is the brightest of the symmetric
// three. A last case holds val:HardClip's threshold to its meaning: the flat top moves with the control.

#include "ExampleRig.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

using valis::test::ExampleRig;

namespace {

constexpr double kRate = ExampleRig::kRate;
constexpr double kF0 = 130.8128;   // C3, the note played
enum Curve { hard = 0, soft, arc, diode, triode };

std::vector<float> play(int curve, int sourceWave, float threshold = 0.5f)
{
    ExampleRig rig("distortion-bench.ttl");
    rig.setParam("curve", static_cast<float>(curve));
    rig.setParam("source_wave", static_cast<float>(sourceWave));
    rig.setParam("threshold", threshold);
    rig.engine.noteOn(48, 1.0f);
    const auto out = rig.render(1.0);
    for (const float v : out)
        assert(std::isfinite(v));
    return std::vector<float>(out.begin() + static_cast<std::ptrdiff_t>(0.25 * kRate),
                              out.begin() + static_cast<std::ptrdiff_t>(0.85 * kRate));
}

/// Amplitude of the harmonic `k` of the note, from a Hann-windowed sum at that frequency.
double harmonic(const std::vector<float>& x, int k)
{
    double re = 0.0, im = 0.0, norm = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(x.size() - 1));
        const double phase = 2.0 * std::numbers::pi * kF0 * k * static_cast<double>(i) / kRate;
        re += w * x[i] * std::cos(phase);
        im += w * x[i] * std::sin(phase);
        norm += w;
    }
    return 2.0 * std::hypot(re, im) / norm;
}

double rmsDb(const std::vector<float>& x)
{
    double sum = 0.0;
    for (const float v : x)
        sum += static_cast<double>(v) * v;
    return 10.0 * std::log10(sum / static_cast<double>(x.size()) + 1e-20);
}

/// Energy in the even harmonics 2 to 12 over the odd ones 3 to 11.
double evenOverOdd(const std::vector<float>& x)
{
    double even = 0.0, odd = 0.0;
    for (int k = 2; k <= 12; ++k)
        (k % 2 == 0 ? even : odd) += std::pow(harmonic(x, k), 2.0);
    return even / odd;
}

/// Share of the harmonic energy at the fifth and above.
double brightness(const std::vector<float>& x)
{
    double high = 0.0, all = 0.0;
    for (int k = 1; k <= 12; ++k)
    {
        const double e = std::pow(harmonic(x, k), 2.0);
        all += e;
        if (k >= 5)
            high += e;
    }
    return high / all;
}

}  // namespace

int main()
{
    // nothing sounds until a note is played
    {
        ExampleRig rig("distortion-bench.ttl");
        const auto quiet = rig.render(0.5);
        assert(*std::max_element(quiet.begin(), quiet.end()) < 1e-4f && *std::min_element(quiet.begin(), quiet.end()) > -1e-4f);
    }

    // a change of curve changes the tone and not the loudness
    double level[5];
    for (int c = 0; c < 5; ++c)
        level[c] = rmsDb(play(c, 1));
    for (int c = 0; c < 5; ++c)
    {
        assert(level[c] > -20.0 && level[c] < -6.0);
        for (int d = c + 1; d < 5; ++d)
            assert(std::abs(level[c] - level[d]) < 1.5);
    }

    // measured on a sine
    const auto sine = [](int c) { return play(c, 0); };
    for (const int c : {hard, soft, arc})
        assert(evenOverOdd(sine(c)) < 0.001);
    assert(evenOverOdd(sine(diode)) > 3.0);
    assert(evenOverOdd(sine(triode)) > 3.0);

    // the hard clip is the brightest of the symmetric curves, and by a margin
    const double hardBright = brightness(sine(hard));
    assert(hardBright > 2.0 * brightness(sine(soft)));
    assert(hardBright > 2.0 * brightness(sine(arc)));
    // a hard clip of a sine is close to a square wave: the third harmonic is a third of the first, the fifth a fifth
    const auto square = sine(hard);
    assert(std::abs(harmonic(square, 3) / harmonic(square, 1) - 1.0 / 3.0) < 0.1);
    assert(std::abs(harmonic(square, 5) / harmonic(square, 1) - 1.0 / 5.0) < 0.1);

    // the threshold is where the top is flat: half the threshold, half the level
    {
        const double wide = harmonic(play(hard, 0, 0.5f), 1);
        const double narrow = harmonic(play(hard, 0, 0.25f), 1);
        assert(std::abs(narrow / wide - 0.5) < 0.05);
    }

    std::puts("DistortionBenchTest passed");
    return 0;
}

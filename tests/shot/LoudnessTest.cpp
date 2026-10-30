// tests/shot/LoudnessTest.cpp
//
// valis::measureLoudness against signals whose answer is published: a 997 Hz stereo sine at -23 dBFS is
// -23.0 LUFS (EBU Tech 3341), silence is not counted, and a sample peak between two samples is found.

#include "valis/Loudness.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

std::vector<float> sine(double hz, double seconds, double dbfs, double phase = 0.0)
{
    std::vector<float> out(static_cast<std::size_t>(kRate * seconds));
    const double a = std::pow(10.0, dbfs / 20.0);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(a * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate + phase));
    return out;
}

void testReferenceTone()
{
    const auto tone = sine(997.0, 10.0, -23.0);
    const auto r = measureLoudness(tone, tone, kRate);
    std::printf("  loudness: -23 dBFS stereo sine reads %.2f LUFS\n", r.integratedLufs);
    assert(std::abs(r.integratedLufs + 23.0) < 0.15);
    assert(std::abs(measureLoudness(tone, {}, kRate).integratedLufs - r.integratedLufs) < 0.01);   // mono counts as both
    assert(r.rangeLu < 0.5);
}

void testSilenceAndGate()
{
    assert(measureLoudness(std::vector<float>(480000, 0.0f), {}, kRate).integratedLufs <= -100.0);

    auto tone = sine(997.0, 10.0, -23.0);
    const auto alone = measureLoudness(tone, tone, kRate).integratedLufs;
    tone.resize(tone.size() * 2, 0.0f);                       // as much silence again: the gate ignores it
    const auto padded = measureLoudness(tone, tone, kRate).integratedLufs;
    std::printf("  loudness: tone %.2f LUFS, with silence after it %.2f\n", alone, padded);
    assert(std::abs(alone - padded) < 0.2);   // blocks straddling the edge count
}

void testRange()
{
    auto loud = sine(997.0, 9.0, -20.0);
    const auto quiet = sine(997.0, 9.0, -32.0);
    loud.insert(loud.end(), quiet.begin(), quiet.end());
    const auto r = measureLoudness(loud, loud, kRate);
    std::printf("  loudness: two levels 12 dB apart, range %.1f LU\n", r.rangeLu);
    assert(r.rangeLu > 9.0 && r.rangeLu < 13.0);
}

void testTruePeak()
{
    // A tone at a quarter of the sample rate and 45 degrees: every sample is 0.707 of the peak, the wave between them is 1.0.
    const auto tone = sine(kRate / 4.0, 1.0, 0.0, kPi / 4.0);
    float samplePeak = 0.0f;
    for (float v : tone)
        samplePeak = std::max(samplePeak, std::abs(v));
    const auto r = measureLoudness(tone, tone, kRate);
    std::printf("  loudness: sample peak %.2f, true peak %.2f dBTP\n", 20.0 * std::log10(samplePeak), r.truePeakDbtp);
    assert(samplePeak < 0.72f);
    assert(r.truePeakDbtp > -0.6 && r.truePeakDbtp < 0.6);
}

}  // namespace

int main()
{
    testReferenceTone();
    testSilenceAndGate();
    testRange();
    testTruePeak();
    std::printf("LoudnessTest PASSED\n");
    return 0;
}

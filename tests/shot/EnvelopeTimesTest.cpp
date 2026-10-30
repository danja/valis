// tests/shot/EnvelopeTimesTest.cpp
//
// valis::measureEnvelope on a struck decay with a known time constant, a slow swell, and a sustained tone.

#include "valis/EnvelopeTimes.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

std::vector<float> shaped(double seconds, double lead, double rise, double tau)
{
    std::vector<float> out(static_cast<std::size_t>(kRate * seconds), 0.0f);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const double t = static_cast<double>(i) / kRate - lead;
        if (t < 0.0)
            continue;
        const double up = rise > 0.0 ? std::min(1.0, t / rise) : 1.0;
        const double down = tau > 0.0 ? std::exp(-t / tau) : 1.0;
        out[i] = static_cast<float>(0.5 * up * down * std::sin(2.0 * kPi * 440.0 * t));
    }
    return out;
}

void testStruck()
{
    const auto r = measureEnvelope(shaped(1.0, 0.1, 0.0, 0.02), kRate);   // 30 dB down after 3.45 time constants: 69 ms
    std::printf("  envelope: struck: starts %.0f ms, attack %.0f ms, decay %.0f ms\n", r.attackAtMs, r.attackMs, r.decayMs);
    assert(std::abs(r.attackAtMs - 100.0) < 8.0);
    assert(r.attackMs < 12.0);
    assert(r.decayMs > 55.0 && r.decayMs < 95.0);
}

void testSwell()
{
    const auto r = measureEnvelope(shaped(2.0, 0.0, 0.25, 0.0), kRate);
    std::printf("  envelope: swell over 250 ms reads attack %.0f ms\n", r.attackMs);
    assert(r.attackMs > 120.0 && r.attackMs < 260.0);
}

void testSustainedHasNoDecay()
{
    const auto r = measureEnvelope(shaped(1.0, 0.0, 0.0, 0.0), kRate);
    assert(r.decayMs < 0.0);
}

void testNoiseFloor()
{
    auto x = shaped(1.0, 0.0, 0.0, 0.02);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] += 0.001f * static_cast<float>(std::sin(static_cast<double>(i) * 1.7));
    const auto r = measureEnvelope(x, kRate);
    std::printf("  envelope: floor %.1f dBFS, dynamic range %.1f dB\n", r.noiseFloorDbfs, r.dynamicRangeDb);
    assert(r.noiseFloorDbfs < -55.0 && r.noiseFloorDbfs > -70.0);
    assert(r.dynamicRangeDb > 30.0);
}

}  // namespace

int main()
{
    testStruck();
    testSwell();
    testSustainedHasNoDecay();
    testNoiseFloor();
    std::printf("EnvelopeTimesTest PASSED\n");
    return 0;
}

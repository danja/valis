// tests/dsp/DriftTest.cpp
//
// val:Drift. A drift is only worth having if it has the statistics it claims: it
// is centred, it spreads by `amount`, it forgets at `speed`, and it does not
// depend on how the engine slices time. It must also be repeatable from its seed.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

/// One control value per block.
std::vector<float> trace(ElementTestFixture& rig, int blocks, int blockSize)
{
    const std::vector<float> block(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(blocks));
    for (int i = 0; i < blocks; ++i)
    {
        rig.run(block);
        out.push_back(rig.lastControlOut[0]);
    }
    return out;
}

double mean(const std::vector<float>& v)
{
    double sum = 0.0;
    for (float x : v) sum += x;
    return sum / static_cast<double>(v.size());
}

double deviation(const std::vector<float>& v)
{
    const double m = mean(v);
    double sum = 0.0;
    for (float x : v) sum += (x - m) * (x - m);
    return std::sqrt(sum / static_cast<double>(v.size()));
}

double correlationAtLag(const std::vector<float>& v, std::size_t lag)
{
    const double m = mean(v);
    double top = 0.0, bottom = 0.0;
    for (std::size_t i = 0; i + lag < v.size(); ++i)
        top += (v[i] - m) * (v[i + lag] - m);
    for (float x : v)
        bottom += (x - m) * (x - m);
    return (top / static_cast<double>(v.size() - lag)) / (bottom / static_cast<double>(v.size()));
}

void testCentredAndBounded()
{
    ElementTestFixture rig("Drift", kRate);
    rig.set("amount", 12.0f);
    rig.set("speed", 5.0f);

    const auto v = trace(rig, 300000, 32);
    float peak = 0.0f;
    for (float x : v)
    {
        assert(std::isfinite(x));
        peak = std::max(peak, std::abs(x));
    }
    std::printf("  mean %.3f, deviation %.3f of 12, peak %.2f of 36\n", mean(v), deviation(v), peak);
    assert(peak <= 36.0f);
    assert(std::abs(mean(v)) < 0.15 * 12.0);
    assert(std::abs(deviation(v) - 12.0) < 0.15 * 12.0);
}

void testItForgetsAtTheSpeedAsked()
{
    // Correlation at a lag of 1 / speed falls to 1/e.
    for (const float speed : { 2.0f, 8.0f })
    {
        ElementTestFixture rig("Drift", kRate);
        rig.set("speed", speed);

        const auto v = trace(rig, 400000, 32);
        const auto lag = static_cast<std::size_t>(kRate / 32.0 / speed);
        const double c = correlationAtLag(v, lag);
        std::printf("  speed %.0f: correlation %.3f at 1/speed (expected 0.368)\n", speed, c);
        assert(std::abs(c - 0.368) < 0.1);
    }
}

void testBlockLengthDoesNotMatter()
{
    ElementTestFixture small("Drift", kRate);
    small.set("speed", 2.0f);
    ElementTestFixture large("Drift", kRate);
    large.set("speed", 2.0f);

    const auto a = trace(small, 300000, 32);
    const auto b = trace(large, 18750, 512);
    std::printf("  deviation %.3f at 32 samples, %.3f at 512\n", deviation(a), deviation(b));
    assert(std::abs(deviation(a) - deviation(b)) < 0.15);
}

void testSeedsRepeatAndDiffer()
{
    ElementTestFixture rig("Drift", kRate);
    rig.set("seed", 7.0f);
    const auto first = trace(rig, 2000, 32);

    rig.reset();
    assert(trace(rig, 2000, 32) == first);

    rig.set("seed", 8.0f);
    assert(trace(rig, 2000, 32) != first);
}

void testCentreMovesTheWholeSignal()
{
    ElementTestFixture plain("Drift", kRate);
    ElementTestFixture moved("Drift", kRate);
    moved.set("centre", 4000.0f);

    const auto a = trace(plain, 5000, 32);
    const auto b = trace(moved, 5000, 32);
    for (std::size_t i = 0; i < a.size(); ++i)
        assert(std::abs(b[i] - (a[i] + 4000.0f)) < 0.01f);
}

void testZeroAmountIsStill()
{
    ElementTestFixture rig("Drift", kRate);
    rig.set("amount", 0.0f);
    rig.set("centre", 7.0f);
    for (float x : trace(rig, 1000, 32))
        assert(x == 7.0f);
}

}  // namespace

int main()
{
    testCentredAndBounded();
    testItForgetsAtTheSpeedAsked();
    testBlockLengthDoesNotMatter();
    testSeedsRepeatAndDiffer();
    testCentreMovesTheWholeSignal();
    testZeroAmountIsStill();

    std::printf("DriftTest PASSED\n");
    return 0;
}

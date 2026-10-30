// tests/dsp/ShakerTest.cpp
//
// val:Shaker. A particle shaker is only worth having if it behaves like a container of beads: silent until it is
// shaken, louder for a harder shake, dying away as the energy goes, and made of separate collisions rather than a
// continuous hiss. With the collisions made regular it is a guiro, and the tick rate is then the scrape rate.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 4096;

/// Shakes once and returns `blocks` blocks of what follows, concatenated.
std::vector<float> shakeOnce(ElementTestFixture& rig, float velocity, int blocks)
{
    const std::vector<float> silence(static_cast<std::size_t>(kBlock), 0.0f);
    std::vector<float> all;
    rig.setNote(60, velocity, true);
    for (int b = 0; b < blocks; ++b)
    {
        const auto out = rig.run(silence);
        all.insert(all.end(), out.begin(), out.end());
        rig.setNote(60, velocity, false);
    }
    return all;
}

float rmsOf(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to && i < x.size(); ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(sum / std::max<std::size_t>(to - from, 1)));
}

double centroid(const std::vector<float>& signal)
{
    const std::vector<float> slice(signal.begin(), signal.begin() + kBlock);
    const auto bins = ElementTestFixture::computeSpectrum(slice, 12);
    double weighted = 0.0, total = 0.0;
    for (std::size_t b = 1; b < bins.size(); ++b)
    {
        weighted += bins[b] * b * kRate / kBlock;
        total += bins[b];
    }
    return total > 0.0 ? weighted / total : 0.0;
}

/// Autocorrelation of the rectified signal at a lag, normalised by its value at zero: how strongly it repeats.
double repeats(const std::vector<float>& x, int lag)
{
    std::vector<double> e(x.size());
    double mean = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) { e[i] = std::abs(x[i]); mean += e[i]; }
    mean /= static_cast<double>(x.size());
    double zero = 0.0, at = 0.0;
    for (std::size_t i = 0; i + static_cast<std::size_t>(lag) < x.size(); ++i)
    {
        zero += (e[i] - mean) * (e[i] - mean);
        at += (e[i] - mean) * (e[i + static_cast<std::size_t>(lag)] - mean);
    }
    return zero > 0.0 ? at / zero : 0.0;
}

void testSilentUntilShaken()
{
    ElementTestFixture rig("Shaker", kRate);
    const std::vector<float> silence(static_cast<std::size_t>(kBlock), 0.0f);
    for (int i = 0; i < 4; ++i)
        assert(ElementTestFixture::measurePeak(rig.run(silence)) == 0.0f);
}

void testShakeDiesAway()
{
    ElementTestFixture rig("Shaker", kRate);
    const auto out = shakeOnce(rig, 1.0f, 40);
    const float early = rmsOf(out, 0, 6000);
    const float late  = rmsOf(out, out.size() - 20000, out.size());
    std::printf("  shaker: rms %.4f in the first 125 ms, %.6f at the end\n", early, late);
    assert(early > 0.01f);
    assert(late < early * 0.02f);
    for (float s : out)
        assert(std::isfinite(s));
}

void testHarderShakeIsLouder()
{
    ElementTestFixture soft("Shaker", kRate), hard("Shaker", kRate);
    const float quiet = rmsOf(shakeOnce(soft, 0.3f, 6), 0, 12000);
    const float loud  = rmsOf(shakeOnce(hard, 1.0f, 6), 0, 12000);
    std::printf("  shaker: soft %.4f, hard %.4f\n", quiet, loud);
    assert(loud > quiet * 1.8f);
}

void testShellFrequencyIsHeard()
{
    ElementTestFixture low("Shaker", kRate), high("Shaker", kRate);
    low.set("frequency", 2200.0f);
    high.set("frequency", 6500.0f);
    const double a = centroid(shakeOnce(low, 1.0f, 2));
    const double b = centroid(shakeOnce(high, 1.0f, 2));
    std::printf("  shaker: centroid %.0f Hz with the shell at 2200, %.0f Hz at 6500\n", a, b);
    assert(b > a * 1.5);
}

/// Random collisions do not repeat; regular ones do, at the scrape rate.
void testRegularityMakesAGuiro()
{
    const int lag = static_cast<int>(kRate / 50.0);                 // 50 teeth a second
    ElementTestFixture random("Shaker", kRate), regular("Shaker", kRate);
    for (auto* rig : {&random, &regular})
    {
        rig->set("scrape", 50.0f);
        rig->set("shake", 600.0f);
        rig->set("sound", 1.5f);
        rig->set("particles", 200.0f);
    }
    regular.set("regularity", 1.0f);
    const auto a = shakeOnce(random, 1.0f, 6);
    const auto b = shakeOnce(regular, 1.0f, 6);
    const std::vector<float> aa(a.begin(), a.begin() + 14000), bb(b.begin(), b.begin() + 14000);
    const double ra = repeats(aa, lag), rb = repeats(bb, lag);
    std::printf("  shaker: repeats at the scrape period %.2f for random, %.2f for regular\n", ra, rb);
    assert(rb > ra + 0.15);
    assert(rb > 0.25);
}

void testRestartIsDeterministic()
{
    ElementTestFixture rig("Shaker", kRate);
    const auto first = shakeOnce(rig, 0.8f, 3);
    rig.reset();
    const auto second = shakeOnce(rig, 0.8f, 3);
    for (std::size_t i = 0; i < first.size(); ++i)
        assert(first[i] == second[i]);
}

}  // namespace

int main()
{
    testSilentUntilShaken();
    testShakeDiesAway();
    testHarderShakeIsLouder();
    testShellFrequencyIsHeard();
    testRegularityMakesAGuiro();
    testRestartIsDeterministic();
    std::printf("ShakerTest PASSED\n");
    return 0;
}

// tests/dsp/BucketBrigadeTest.cpp
//
// val:BucketBrigade. What makes a BBD echo is that the delay and the bandwidth are
// tied together, that repeats darken, and that the companding is transparent for
// a signal and hides the noise when there is none.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

float rmsOfTail(const std::vector<float>& v, std::size_t n)
{
    return ElementTestFixture::measureRms(std::vector<float>(v.end() - static_cast<std::ptrdiff_t>(n), v.end()));
}

/// Level of a steady sine through the delay, relative to the input, in dB.
double throughDb(double hz, float timeMs, bool compander = true)
{
    ElementTestFixture rig("BucketBrigade", kRate);
    rig.set("time", timeMs);
    rig.set("feedback", 0.0f);
    rig.set("noise", -120.0f);
    rig.set("compander", compander ? 1.0f : 0.0f);

    const auto out = rig.run(ElementTestFixture::generateSine(hz, kRate, 96000, 0.3f));
    return 20.0 * std::log10(rmsOfTail(out, 24000) / (0.3 / std::sqrt(2.0)));
}

void testDelayIsTheTimeAsked()
{
    ElementTestFixture rig("BucketBrigade", kRate);
    rig.set("time", 100.0f);
    rig.set("feedback", 0.0f);
    rig.set("noise", -120.0f);

    // A low-frequency burst, so the filters leave it alone and its centre is the delay.
    std::vector<float> burst(24000, 0.0f);
    for (int i = 0; i < 480; ++i)
        burst[static_cast<std::size_t>(i)] = static_cast<float>(
            0.3 * (0.5 - 0.5 * std::cos(2.0 * M_PI * i / 480)) * std::sin(2.0 * M_PI * 300.0 * i / kRate));
    const auto out = rig.run(burst);

    double sum = 0.0, weighted = 0.0;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const double e = static_cast<double>(out[i]) * out[i];
        sum += e;
        weighted += e * static_cast<double>(i);
    }
    const double ms = weighted / sum / kRate * 1000.0 - 5.0;   // the burst is 10 ms long
    std::printf("  asked 100 ms, burst centre arrived after %.1f ms\n", ms);
    assert(std::abs(ms - 100.0) < 3.0);
}

void testLongDelaysAreDarker()
{
    const double shortDelay = throughDb(6000.0, 50.0f);
    const double longDelay  = throughDb(6000.0, 800.0f);
    std::printf("  6 kHz: %.1f dB at 50 ms, %.1f dB at 800 ms\n", shortDelay, longDelay);
    assert(shortDelay > -3.0);
    assert(longDelay < -30.0);

    // Low frequencies are left alone either way.
    assert(std::abs(throughDb(300.0, 800.0f)) < 1.0);
}

void testRepeatsDarkenAndDecay()
{
    ElementTestFixture rig("BucketBrigade", kRate);
    rig.set("time", 200.0f);
    rig.set("feedback", 0.7f);
    rig.set("noise", -120.0f);
    rig.set("compander", 0.0f);

    // An impulse-like click: bright, so each trip loses top end.
    std::vector<float> click(96000, 0.0f);
    click[0] = 0.5f;
    const auto out = rig.run(click);

    const auto energy = [&](double fromMs, double toMs)
    {
        double e = 0.0;
        for (auto i = static_cast<std::size_t>(fromMs * 48.0); i < static_cast<std::size_t>(toMs * 48.0); ++i)
            e += static_cast<double>(out[i]) * out[i];
        return e;
    };
    const double first = energy(190, 215), second = energy(390, 415), third = energy(590, 615);
    std::printf("  repeat energies %.2e, %.2e, %.2e\n", first, second, third);
    assert(first > 0.0 && second < first && third < second);
}

void testCompanderIsTransparent()
{
    for (const double level : { 0.01, 0.1, 0.5 })
    {
        ElementTestFixture on("BucketBrigade", kRate);
        on.set("time", 80.0f);
        on.set("feedback", 0.0f);
        on.set("noise", -120.0f);
        const auto out = on.run(ElementTestFixture::generateSine(500.0, kRate, 96000, static_cast<float>(level)));
        const double db = 20.0 * std::log10(rmsOfTail(out, 24000) / (level / std::sqrt(2.0)));
        std::printf("  level %.2f: %.2f dB through the compander\n", level, db);
        assert(std::abs(db) < 1.5);
    }
}

void testCompanderHidesTheNoise()
{
    const auto hiss = [](bool compander)
    {
        ElementTestFixture rig("BucketBrigade", kRate);
        rig.set("time", 80.0f);
        rig.set("feedback", 0.0f);
        rig.set("noise", -50.0f);
        rig.set("compander", compander ? 1.0f : 0.0f);
        const auto out = rig.run(std::vector<float>(96000, 0.0f));
        return 20.0 * std::log10(std::max(rmsOfTail(out, 24000), 1.0e-9f));
    };

    const double without = hiss(false), with = hiss(true);
    std::printf("  silence: noise %.1f dB without the compander, %.1f dB with it\n", without, with);
    assert(with < without - 20.0);
}

void testSelfOscillationStaysBounded()
{
    ElementTestFixture rig("BucketBrigade", kRate);
    rig.set("time", 120.0f);
    rig.set("feedback", 1.1f);

    std::vector<float> in(96000, 0.0f);
    in[0] = 0.5f;
    for (const float v : rig.run(in))
        assert(std::isfinite(v) && std::abs(v) < 10.0f);
}

void testSeedsRepeat()
{
    const auto render = [](float seed)
    {
        ElementTestFixture rig("BucketBrigade", kRate);
        rig.set("seed", seed);
        rig.set("noise", -40.0f);
        return rig.run(std::vector<float>(8192, 0.0f));
    };
    assert(render(2.0f) == render(2.0f));
    assert(render(2.0f) != render(3.0f));
}

}  // namespace

int main()
{
    testDelayIsTheTimeAsked();
    testLongDelaysAreDarker();
    testRepeatsDarkenAndDecay();
    testCompanderIsTransparent();
    testCompanderHidesTheNoise();
    testSelfOscillationStaysBounded();
    testSeedsRepeat();

    std::printf("BucketBrigadeTest PASSED\n");
    return 0;
}

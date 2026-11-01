// tests/dsp/OctaveDividerTest.cpp
//
// val:OctaveDivider. The output of a divider must sit exactly one (or two) octaves below the note,
// follow the note's loudness, and stay silent when there is no note. The pitch is read with the
// spectrum, not assumed.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

/// The strongest partial in the last 16384 samples, in Hz.
double strongest(const std::vector<float>& v)
{
    const std::vector<float> tail(v.end() - 16384, v.end());
    const auto bins = ElementTestFixture::computeSpectrum(tail, 14);
    std::size_t peak = 4;
    for (std::size_t b = 5; b + 2 < bins.size(); ++b)
        if (bins[b] > bins[peak]) peak = b;
    return static_cast<double>(peak) * kRate / 16384.0;
}

std::vector<float> run(ElementTestFixture& rig, const std::vector<float>& in)
{
    std::vector<float> out;
    for (std::size_t at = 0; at < in.size(); at += 256)
    {
        const auto end = std::min(at + 256, in.size());
        const auto part = rig.run(std::vector<float>(in.begin() + static_cast<std::ptrdiff_t>(at),
                                                     in.begin() + static_cast<std::ptrdiff_t>(end)));
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

}  // namespace

int main()
{
    for (const double hz : { 110.0, 220.0, 330.0, 440.0 })
    {
        ElementTestFixture one("OctaveDivider", kRate);
        const double down1 = strongest(run(one, ElementTestFixture::generateSine(hz, kRate, 48000, 0.5f)));
        std::printf("  %.0f Hz: one octave %.1f Hz", hz, down1);
        assert(std::abs(down1 - hz / 2.0) < 4.0);

        ElementTestFixture two("OctaveDivider", kRate);
        two.set("division", 1.0f);
        const double down2 = strongest(run(two, ElementTestFixture::generateSine(hz, kRate, 48000, 0.5f)));
        std::printf(", two octaves %.1f Hz\n", down2);
        assert(std::abs(down2 - hz / 4.0) < 3.0);
    }

    // The same division at any level, and at another rate.
    {
        ElementTestFixture quiet("OctaveDivider", kRate);
        const double q = strongest(run(quiet, ElementTestFixture::generateSine(220.0, kRate, 48000, 0.01f)));
        assert(std::abs(q - 110.0) < 4.0);
    }

    // The same division at other host rates.
    for (const double rate : { 44100.0, 96000.0, 192000.0 })
    {
        ElementTestFixture rig("OctaveDivider", rate);
        const int n = static_cast<int>(rate);
        const auto y = run(rig, ElementTestFixture::generateSine(220.0, rate, n, 0.5f));
        // A window the same length in seconds at every rate, so the bins are the same width in hertz.
        const int order = rate > 100000.0 ? 16 : rate > 50000.0 ? 15 : 14;
        const std::vector<float> tail(y.end() - (1 << order), y.end());
        const auto bins = ElementTestFixture::computeSpectrum(tail, order);
        std::size_t peak = 4;
        for (std::size_t b = 5; b + 2 < bins.size(); ++b)
            if (bins[b] > bins[peak]) peak = b;
        const double hz = static_cast<double>(peak) * rate / (1 << order);
        std::printf("  rate %.0f: %.1f Hz\n", rate, hz);
        assert(std::abs(hz - 110.0) < 4.0);
    }

    // The output has the loudness of the input and dies away with it.
    {
        ElementTestFixture rig("OctaveDivider", kRate);
        auto note = ElementTestFixture::generateSine(220.0, kRate, 24000, 0.5f);
        note.resize(48000, 0.0f);
        const auto y = run(rig, note);
        const std::vector<float> loud(y.begin() + 6000, y.begin() + 22000);
        const std::vector<float> after(y.begin() + 33000, y.end());
        const float rmsLoud = ElementTestFixture::measureRms(loud);
        std::printf("  rms while playing %.3f (input 0.354), after %.5f\n", rmsLoud, ElementTestFixture::measureRms(after));
        assert(rmsLoud > 0.2f && rmsLoud < 0.7f);
        assert(ElementTestFixture::measureRms(after) < 0.001f);
    }

    // Silence and low-level noise make no sound.
    {
        ElementTestFixture rig("OctaveDivider", kRate);
        assert(ElementTestFixture::measurePeak(run(rig, std::vector<float>(24000, 0.0f))) == 0.0f);

        std::mt19937 gen(3);
        std::uniform_real_distribution<float> dist(-5.0e-5f, 5.0e-5f);
        std::vector<float> hiss(24000);
        for (auto& s : hiss) s = dist(gen);
        assert(ElementTestFixture::measurePeak(run(rig, hiss)) < 1.0e-3f);
    }

    // Dry and wet.
    {
        ElementTestFixture rig("OctaveDivider", kRate);
        rig.set("wet", 0.0f);
        rig.set("dry", 1.0f);
        const auto input = ElementTestFixture::generateSine(220.0, kRate, 4800, 0.5f);
        const auto y = run(rig, input);
        for (std::size_t i = 0; i < y.size(); ++i)
            assert(std::abs(y[i] - input[i]) < 1.0e-6f);
    }

    std::printf("OctaveDividerTest: ok\n");
    return 0;
}

// tests/dsp/PitchTrackerTest.cpp
//
// val:PitchTracker. A pitch tracker is judged by the pitch it reports (in cents, across the
// range and at every sample rate), by the errors YIN exists to avoid (an octave low on a sound
// with strong harmonics, a missing fundamental), by what it says when there is no pitch
// (silence, noise), and by giving the same answer however the host cut the stream.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace valis;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Reading
{
    float frequency = 0.0f, note = 0.0f, voiced = 0.0f, clarity = 0.0f;
};

/// Feeds `signal` in blocks and returns the outputs after the last one.
Reading track(ElementTestFixture& rig, const std::vector<float>& signal, std::size_t block = 32)
{
    for (std::size_t at = 0; at < signal.size(); at += block)
    {
        const auto end = std::min(at + block, signal.size());
        rig.run(std::vector<float>(signal.begin() + static_cast<std::ptrdiff_t>(at),
                                   signal.begin() + static_cast<std::ptrdiff_t>(end)));
    }
    const auto& o = rig.lastControlOut;
    return { o[0], o[1], o[2], o[3] };
}

double cents(double measured, double expected)
{
    return 1200.0 * std::log2(measured / expected);
}

/// A sum of harmonics of f0 with the given amplitudes, the first being the fundamental.
std::vector<float> harmonics(double f0, const std::vector<double>& amplitudes, double rate, double seconds)
{
    const int n = static_cast<int>(seconds * rate);
    std::vector<float> out(static_cast<std::size_t>(n), 0.0f);
    for (std::size_t h = 0; h < amplitudes.size(); ++h)
        for (int i = 0; i < n; ++i)
            out[static_cast<std::size_t>(i)] += static_cast<float>(
                amplitudes[h] * std::sin(2.0 * kPi * f0 * static_cast<double>(h + 1) * i / rate));
    return out;
}

}  // namespace

int main()
{
    // -- a sine, across the range ---------------------------------------------------------------
    for (const double hz : { 41.2, 55.0, 82.41, 110.0, 196.0, 440.0, 1000.0, 1318.5 })
    {
        ElementTestFixture rig("PitchTracker", 48000.0);
        rig.set("low", 40.0f);
        const auto r = track(rig, ElementTestFixture::generateSine(hz, 48000.0, 24000, 0.5f));
        std::printf("  sine %7.2f Hz -> %8.3f Hz (%+.2f cents), clarity %.3f\n",
                    hz, r.frequency, cents(r.frequency, hz), r.clarity);
        assert(r.voiced > 0.5f);
        assert(std::abs(cents(r.frequency, hz)) < 1.0);
        assert(r.clarity > 0.9f);
        assert(std::abs(r.note - (69.0 + 12.0 * std::log2(hz / 440.0))) < 0.1);
    }

    // -- the same pitch at every rate ---------------------------------------------------------------
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        ElementTestFixture rig("PitchTracker", rate);
        const auto r = track(rig, ElementTestFixture::generateSine(220.0, rate, static_cast<int>(rate * 0.5), 0.5f));
        std::printf("  rate %6.0f: %8.3f Hz (%+.2f cents)\n", rate, r.frequency, cents(r.frequency, 220.0));
        assert(r.voiced > 0.5f);
        assert(std::abs(cents(r.frequency, 220.0)) < 1.0);
    }

    // -- the errors YIN exists to avoid --------------------------------------------------------------
    {
        // A strong second harmonic: the period is still that of the fundamental.
        ElementTestFixture a("PitchTracker");
        const auto strongSecond = track(a, harmonics(110.0, { 0.4, 1.0, 0.5, 0.3, 0.2 }, 48000.0, 0.5));
        assert(strongSecond.voiced > 0.5f);
        assert(std::abs(cents(strongSecond.frequency, 110.0)) < 10.0);

        // No energy at the fundamental at all.
        ElementTestFixture b("PitchTracker");
        const auto missing = track(b, harmonics(130.0, { 0.0, 0.5, 0.5, 0.5, 0.4, 0.3, 0.2 }, 48000.0, 0.5));
        std::printf("  missing fundamental: %.2f Hz\n", missing.frequency);
        assert(missing.voiced > 0.5f);
        assert(std::abs(cents(missing.frequency, 130.0)) < 10.0);

        // A sawtooth, every harmonic.
        ElementTestFixture c("PitchTracker");
        std::vector<double> saw;
        for (int h = 1; h <= 40; ++h) saw.push_back(0.2 / h);
        const auto sawtooth = track(c, harmonics(150.0, saw, 48000.0, 0.5));
        assert(sawtooth.voiced > 0.5f);
        assert(std::abs(cents(sawtooth.frequency, 150.0)) < 10.0);
    }

    // -- no pitch: silence and noise ------------------------------------------------------------------
    {
        ElementTestFixture silent("PitchTracker");
        const auto s = track(silent, std::vector<float>(24000, 0.0f));
        assert(s.voiced < 0.5f && s.frequency == 0.0f);

        ElementTestFixture noisy("PitchTracker");
        std::mt19937 gen(5);
        std::uniform_real_distribution<float> dist(-0.4f, 0.4f);
        std::vector<float> noise(48000);
        for (auto& v : noise) v = dist(gen);
        const auto n = track(noisy, noise);
        std::printf("  noise: voiced %.0f, clarity %.2f\n", n.voiced, n.clarity);
        assert(n.voiced < 0.5f);
        assert(n.clarity < 0.6f);
    }

    // -- holds the last pitch when the sound stops; follows a new note ---------------------------------------
    {
        ElementTestFixture rig("PitchTracker");
        auto signal = ElementTestFixture::generateSine(220.0, 48000.0, 24000, 0.5f);
        track(rig, signal);
        const auto held = track(rig, std::vector<float>(24000, 0.0f));
        assert(held.voiced < 0.5f);
        assert(std::abs(cents(held.frequency, 220.0)) < 6.0);

        // 220 Hz, then 330 Hz without a gap: within 100 ms the new note has been found.
        const auto next = track(rig, ElementTestFixture::generateSine(330.0, 48000.0, 4800, 0.5f));
        assert(next.voiced > 0.5f);
        assert(std::abs(cents(next.frequency, 330.0)) < 10.0);
    }

    // -- the answer does not depend on the block size ---------------------------------------------------------
    {
        const auto signal = harmonics(196.0, { 1.0, 0.5, 0.3 }, 48000.0, 0.4);
        Reading first;
        bool have = false;
        for (const std::size_t block : { 1, 7, 32, 100, 512, 4096 })
        {
            ElementTestFixture rig("PitchTracker");
            const auto r = track(rig, signal, block);
            if (! have) { first = r; have = true; }
            assert(r.frequency == first.frequency);
            assert(r.voiced == first.voiced);
            assert(r.clarity == first.clarity);
        }
    }

    // -- reset forgets ----------------------------------------------------------------------------------------------
    {
        ElementTestFixture rig("PitchTracker");
        track(rig, ElementTestFixture::generateSine(440.0, 48000.0, 24000, 0.5f));
        rig.reset();
        const auto r = track(rig, std::vector<float>(480, 0.0f));
        assert(r.frequency == 0.0f && r.voiced < 0.5f);
    }

    std::printf("PitchTrackerTest: ok\n");
    return 0;
}

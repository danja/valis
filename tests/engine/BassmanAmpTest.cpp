// tests/engine/BassmanAmpTest.cpp
//
// circuits/bassman-amp.ttl (scripts/elements-demo/bassman-amp.py), played through the engine: silent until a note,
// in tune, still ringing after the note is released, and the passive tone stack behaving as val:ToneStack describes.
// Bass, middle and treble each move their own band by several decibels; the middle control also moves the treble
// (the network interacts, which is why it is not an equaliser); and driving the input squeezes the peaks.

#include "ExampleRig.h"
#include "Fft.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <vector>

using valis::test::ExampleRig;
using valis::test::fft;

namespace {

constexpr double kRate = ExampleRig::kRate;

struct Bands { double bass, mid, treble, rms, peak; };

/// A note played for `held` seconds, analysed from 30 ms to 0.5 s. `settings` are Controls-panel parameters.
std::vector<float> play(int note, double held, double seconds, const std::vector<std::pair<const char*, float>>& settings)
{
    ExampleRig rig("bassman-amp.ttl");
    for (const auto& [symbol, value] : settings)
        rig.setParam(symbol, value);
    rig.engine.noteOn(note, 1.0f);
    auto out = rig.render(held);
    rig.engine.noteOff(note);
    const auto tail = rig.render(seconds - held);
    out.insert(out.end(), tail.begin(), tail.end());
    for (const float v : out)
        assert(std::isfinite(v));
    return out;
}

double bandDb(const std::vector<float>& x, double lo, double hi)
{
    const std::size_t n = 32768;
    std::vector<std::complex<double>> a(n, 0.0);
    const std::size_t start = static_cast<std::size_t>(0.03 * kRate);
    const std::size_t len = std::min(x.size() - start, static_cast<std::size_t>(0.47 * kRate));
    for (std::size_t i = 0; i < len; ++i)
        a[i] = x[start + i] * (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(len - 1)));
    fft(a);
    double sum = 0.0;
    for (std::size_t k = 0; k < n / 2; ++k)
    {
        const double f = static_cast<double>(k) * kRate / static_cast<double>(n);
        if (f >= lo && f < hi)
            sum += std::norm(a[k]);
    }
    return 10.0 * std::log10(sum + 1e-20);
}

Bands analyse(const std::vector<float>& x)
{
    Bands b{bandDb(x, 60, 250), bandDb(x, 400, 1000), bandDb(x, 2500, 6000), 0.0, 0.0};
    double sum = 0.0;
    const std::size_t start = static_cast<std::size_t>(0.03 * kRate), end = static_cast<std::size_t>(0.5 * kRate);
    for (std::size_t i = start; i < end; ++i)
    {
        sum += static_cast<double>(x[i]) * x[i];
        b.peak = std::max(b.peak, static_cast<double>(std::abs(x[i])));
    }
    b.rms = std::sqrt(sum / static_cast<double>(end - start));
    return b;
}

/// The strongest frequency between lo and hi, from a long FFT of the note's first half second.
double peakFrequency(const std::vector<float>& x, double lo, double hi)
{
    const std::size_t n = 65536;
    std::vector<std::complex<double>> a(n, 0.0);
    const std::size_t len = std::min(x.size(), static_cast<std::size_t>(0.6 * kRate));
    for (std::size_t i = 0; i < len; ++i)
        a[i] = x[i] * (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(len - 1)));
    fft(a);
    double best = 0.0, bestF = 0.0;
    for (std::size_t k = 1; k < n / 2; ++k)
    {
        const double f = static_cast<double>(k) * kRate / static_cast<double>(n);
        if (f >= lo && f < hi && std::norm(a[k]) > best)
            best = std::norm(a[k]), bestF = f;
    }
    return bestF;
}

}  // namespace

int main()
{
    // silent until played
    {
        ExampleRig rig("bassman-amp.ttl");
        const auto quiet = rig.render(0.5);
        assert(*std::max_element(quiet.begin(), quiet.end()) < 1e-4f && *std::min_element(quiet.begin(), quiet.end()) > -1e-4f);
    }

    // in tune: E2 (40) and E4 (64), to within 1.5 per cent
    for (const auto& [note, hz] : {std::pair{40, 82.41}, std::pair{64, 329.63}})
    {
        const auto x = play(note, 0.8, 1.0, {});
        assert(std::abs(peakFrequency(x, hz * 0.8, hz * 1.25) / hz - 1.0) < 0.015);
    }

    // a plucked string rings on after the note is released
    {
        const auto x = play(48, 0.2, 1.0, {});
        double sum = 0.0;
        for (std::size_t i = static_cast<std::size_t>(0.4 * kRate); i < static_cast<std::size_t>(0.6 * kRate); ++i)
            sum += static_cast<double>(x[i]) * x[i];
        assert(std::sqrt(sum / (0.2 * kRate)) > 0.003);
    }

    // the tone stack, with the input turned down so the valve stays near linear
    const auto stack = [](float treble, float middle, float bass)
    {
        return analyse(play(48, 0.8, 1.0, {{"input", -12.0f}, {"treble", treble}, {"middle", middle}, {"bass", bass}}));
    };
    const auto bassLow = stack(0.5f, 0.5f, 0.0f), bassHigh = stack(0.5f, 0.5f, 1.0f);
    const auto midLow = stack(0.5f, 0.0f, 0.5f), midHigh = stack(0.5f, 1.0f, 0.5f);
    const auto trebLow = stack(0.0f, 0.5f, 0.5f), trebHigh = stack(1.0f, 0.5f, 0.5f);

    assert(bassHigh.bass - bassLow.bass > 4.0);
    assert(midHigh.mid - midLow.mid > 5.0);
    assert(trebHigh.treble - trebLow.treble > 8.0);
    // each control leaves the far band alone: bass does not touch the treble, treble does not touch the bass
    assert(std::abs(bassHigh.treble - bassLow.treble) < 1.5);
    assert(std::abs(trebHigh.bass - trebLow.bass) < 1.0);
    // but the middle control moves the treble as well, which is the interaction of the passive network
    assert(midHigh.treble - midLow.treble > 2.0);

    // driving the input squeezes the peaks: a lower peak-to-rms ratio, and a higher level out of the valve
    const auto soft = analyse(play(48, 0.8, 1.0, {{"input", -12.0f}}));
    const auto hard = analyse(play(48, 0.8, 1.0, {{"input", 24.0f}}));
    assert(soft.peak / soft.rms - hard.peak / hard.rms > 0.5);
    assert(hard.rms > 4.0 * soft.rms);

    std::puts("BassmanAmpTest passed");
    return 0;
}

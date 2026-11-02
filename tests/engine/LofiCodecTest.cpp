// tests/engine/LofiCodecTest.cpp
//
// circuits/lofi-codec.ttl (scripts/elements-demo/lofi-codec.py), played through the engine: silent until a note, and each
// control doing to the spectrum what the header says. With the codec's floor, step and bandwidth out of the way it is
// the original sound; Bandwidth removes what is above it; Floor drops the weak partials, which leaves the spaces between
// the harmonics emptier; Step puts rounding noise into them; and the spectral gate takes hiss from between the
// harmonics and leaves the note. Measured on a held C3, in a window after the codec's latency has passed.

#include "ExampleRig.h"
#include "Fft.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using valis::test::ExampleRig;

namespace {

constexpr double kRate = ExampleRig::kRate;
constexpr double kF0 = 130.8128;

struct Spectrum
{
    double above4k = 0.0;     ///< share of the energy above 4 kHz, in decibels
    double between = 0.0;     ///< mean level midway between harmonics 20 to 50, relative to the loudest bin
    double fundamental = 0.0; ///< the first harmonic, relative to the loudest bin
    double rms = 0.0;
    std::vector<float> samples;  ///< the analysed window
    double harmonics[30] = {};  ///< levels of harmonics 1 to 30, relative to the loudest bin
};

Spectrum measure(const std::vector<std::pair<const char*, float>>& settings)
{
    ExampleRig rig("lofi-codec.ttl");
    // the codec and the gate are neutral unless a case says otherwise
    for (const auto& [symbol, value] : {std::pair<const char*, float>{"bandwidth", 24000.0f}, {"floor", 0.0f}, {"step", 0.0f},
                                         {"codec_mix", 1.0f}, {"gate_threshold", -120.0f}, {"hiss", -30.0f}})
        rig.setParam(symbol, value);
    for (const auto& [symbol, value] : settings)
        rig.setParam(symbol, value);
    rig.engine.noteOn(48, 1.0f);
    const auto all = rig.render(1.5);
    for (const float v : all)
        assert(std::isfinite(v));

    const std::size_t start = static_cast<std::size_t>(0.5 * kRate), len = static_cast<std::size_t>(0.9 * kRate), n = 65536;
    std::vector<std::complex<double>> a(n, 0.0);
    double sum = 0.0;
    for (std::size_t i = 0; i < len; ++i)
    {
        a[i] = all[start + i] * (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * static_cast<double>(i) / static_cast<double>(len - 1)));
        sum += static_cast<double>(all[start + i]) * all[start + i];
    }
    valis::test::fft(a);

    std::vector<double> power(n / 2);
    double total = 0.0, high = 0.0, peak = 0.0;
    for (std::size_t k = 0; k < n / 2; ++k)
    {
        power[k] = std::norm(a[k]);
        total += power[k];
        if (static_cast<double>(k) * kRate / static_cast<double>(n) > 4000.0)
            high += power[k];
        peak = std::max(peak, power[k]);
    }
    const auto levelAt = [&](double hz)
    {
        const auto k = static_cast<std::size_t>(std::lround(hz / kRate * static_cast<double>(n)));
        return 10.0 * std::log10(power[k] / peak + 1e-20);
    };

    Spectrum s;
    s.above4k = 10.0 * std::log10(high / total + 1e-20);
    for (int h = 20; h < 50; ++h)
        s.between += levelAt(kF0 * (h + 0.5)) / 30.0;
    s.fundamental = levelAt(kF0);
    for (int h = 1; h <= 30; ++h)
        s.harmonics[h - 1] = levelAt(kF0 * h);
    s.rms = std::sqrt(sum / static_cast<double>(len));
    s.samples.assign(all.begin() + static_cast<std::ptrdiff_t>(start), all.begin() + static_cast<std::ptrdiff_t>(start + len));
    return s;
}

}  // namespace

int main()
{
    // silent until played
    {
        ExampleRig rig("lofi-codec.ttl");
        const auto quiet = rig.render(0.5);
        assert(*std::max_element(quiet.begin(), quiet.end()) < 1e-4f && *std::min_element(quiet.begin(), quiet.end()) > -1e-4f);
    }

    const auto clean = measure({});
    assert(clean.rms > 0.05);

    // the mix control puts the original back, however hard the codec is set
    const auto mixedOut = measure({{"bandwidth", 4000.0f}, {"floor", 40.0f}, {"step", 12.0f}, {"codec_mix", 0.0f}});
    assert(std::abs(mixedOut.above4k - clean.above4k) < 1.0 && std::abs(mixedOut.rms - clean.rms) < 0.02);

    // bandwidth removes what is above it
    const auto narrow = measure({{"bandwidth", 4000.0f}});
    assert(clean.above4k - narrow.above4k > 15.0);

    // floor drops the weak partials: the high ones go, and so do the spaces between the harmonics
    const auto floored = measure({{"floor", 20.0f}});
    assert(clean.above4k - floored.above4k > 15.0);
    assert(clean.between - floored.between > 8.0);

    // step rounds the level of every bin to a multiple of the step, which is noise added to the sound: the error against
    // the unprocessed sound is larger the coarser the step, and nil when it is out of the way
    const auto errorDb = [&](const Spectrum& x)
    {
        double e = 0.0, r = 0.0;
        for (std::size_t i = 0; i < x.samples.size(); ++i)
        {
            const double d = static_cast<double>(x.samples[i]) - clean.samples[i];
            e += d * d;
            r += static_cast<double>(clean.samples[i]) * clean.samples[i];
        }
        return 10.0 * std::log10(e / r + 1e-20);
    };
    const double fine = errorDb(measure({{"step", 2.0f}})), coarse = errorDb(measure({{"step", 12.0f}}));
    std::fprintf(stderr, "step error: %.1f dB at 2, %.1f dB at 12; unchanged %.1f\n", fine, coarse, errorDb(measure({})));
    assert(errorDb(measure({})) < -60.0);
    assert(coarse > fine + 3.0);
    assert(coarse > -25.0);

    // the gate takes hiss out from between the harmonics, and the note is left where it was
    const auto hissy = measure({{"hiss", -20.0f}});
    const auto gated = measure({{"hiss", -20.0f}, {"gate_threshold", -40.0f}});
    assert(hissy.between - clean.between > 6.0);       // the hiss is really there
    assert(hissy.between - gated.between > 10.0);      // and the gate removes it
    assert(std::abs(gated.fundamental - hissy.fundamental) < 2.0);

    std::puts("LofiCodecTest passed");
    return 0;
}

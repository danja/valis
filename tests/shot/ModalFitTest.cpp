// tests/shot/ModalFitTest.cpp
//
// fitModes must recover the modes of a sound whose modes are known. The sound is made by renderModes, which
// FreeModalBankTest holds to the element's own impulse response, then given a quiet start and some noise.

#include "valis/ModalFit.h"
#include "valis/SpectralDistance.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> struck(const std::vector<FittedMode>& modes, double noiseLevel)
{
    // A tenth of a second of silence, then the strike, then 1.6 s of ring, with a little hiss throughout.
    const auto lead = static_cast<std::size_t>(0.1 * kRate);
    const auto ring = renderModes(modes, kRate, static_cast<std::size_t>(1.6 * kRate));
    std::vector<float> x(lead + ring.size(), 0.0f);
    std::srand(7);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double hiss = (static_cast<double>(std::rand()) / RAND_MAX * 2.0 - 1.0) * noiseLevel;
        x[i] = static_cast<float>(hiss) + (i >= lead ? ring[i - lead] : 0.0f);
    }
    return x;
}

const FittedMode* nearest(const std::vector<FittedMode>& found, double hertz)
{
    const FittedMode* best = nullptr;
    for (const auto& m : found)
        if (best == nullptr || std::abs(m.hertz - hertz) < std::abs(best->hertz - hertz))
            best = &m;
    return best;
}

void testKnownModesAreRecovered()
{
    const std::vector<FittedMode> truth{{523.0, 1.5, 0.30}, {1318.0, 0.8, 0.15}, {2410.0, 0.5, 0.08}};
    const auto found = fitModes(struck(truth, 0.0005), kRate, 6);
    assert(found.size() >= 3);

    for (const auto& t : truth)
    {
        const auto* m = nearest(found, t.hertz);
        assert(m != nullptr);
        std::printf("  mode %.0f Hz %.2f s %.3f -> %.1f Hz %.2f s %.3f\n", t.hertz, t.seconds, t.gain, m->hertz, m->seconds, m->gain);
        assert(std::abs(m->hertz - t.hertz) < t.hertz * 0.002);     // within 0.2 percent
        assert(m->seconds > t.seconds * 0.75 && m->seconds < t.seconds * 1.3);
        assert(m->gain > t.gain * 0.7 && m->gain < t.gain * 1.4);
    }

    // The fitted bank sounds like the sound: a small spectral distance, where an empty guess would be large.
    const auto sound = struck(truth, 0.0);
    const std::vector<float> heard(sound.begin() + static_cast<std::ptrdiff_t>(0.1 * kRate), sound.end());
    const auto distance = spectralDistance(heard, renderModes(found, kRate, heard.size()), kRate).meanDb;
    std::printf("  spectral distance of the fit: %.2f dB\n", distance);
    assert(distance < 6.0);
}

void testTheStringFormIsReadableByTheElement()
{
    const std::vector<FittedMode> modes{{523.0, 1.5, 0.3}, {1318.0, 0.8, 0.15}};
    const auto text = modesToString(modes);
    assert(text == "523.00:1.500:0.3 1318.00:0.800:0.15");
}

void testSilenceAndShortSoundsGiveNothing()
{
    assert(fitModes(std::vector<float>(48000, 0.0f), kRate, 8).empty());
    assert(fitModes(std::vector<float>(1000, 0.5f), kRate, 8).empty());
    assert(fitModes(struck({{523.0, 1.0, 0.3}}, 0.0), kRate, 0).empty());
}

void testNoiseHasNoModesWorthTrusting()
{
    // Hiss has peaks, but nothing that decays: the ring time reads as the longest allowed, never a short one.
    std::vector<float> hiss(static_cast<std::size_t>(2.0 * kRate));
    std::srand(3);
    for (auto& v : hiss)
        v = static_cast<float>(std::rand()) / RAND_MAX * 0.5f - 0.25f;
    for (const auto& m : fitModes(hiss, kRate, 8))
        assert(m.seconds > 0.0 && m.seconds <= 60.0 && std::isfinite(m.gain));
}

}  // namespace

int main()
{
    testKnownModesAreRecovered();
    testTheStringFormIsReadableByTheElement();
    testSilenceAndShortSoundsGiveNothing();
    testNoiseHasNoModesWorthTrusting();
    std::puts("ModalFitTest passed");
    return 0;
}

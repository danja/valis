// tests/dsp/SpectralTest.cpp
//
// val:SpectralGate, and through it the short-time Fourier transform the block
// domain is built on. The first thing to establish is that a frame nothing
// touches comes back unchanged: an overlap-add that does not sum to unity would
// make every other measurement here meaningless.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate   = 48000.0;
constexpr int    kWindow = 1024;   ///< the default val:fftSize

/// A tone with a whole number of cycles in `n` samples. The fixture is run with
/// the same buffer several times over, so a tone that does not close the block
/// exactly would click at every join, and the click would smear across the
/// spectrum and be measured as noise the gate had supposedly added.
std::vector<float> exactTone(int cycles, int n, float amplitude)
{
    return ElementTestFixture::generateSine(kRate * cycles / n, kRate, n, amplitude);
}

/// Runs `blocks` blocks through the element and returns the last one, so the
/// measurement is past the window of latency the transform costs.
std::vector<float> settled(ElementTestFixture& rig,
                           const std::vector<float>& input,
                           int blocks = 6)
{
    std::vector<float> out;
    for (int i = 0; i < blocks; ++i)
        out = rig.run(input);
    return out;
}

/// White noise at a known amplitude. Deterministic, so a failure is repeatable.
std::vector<float> noise(int n, float amplitude, std::uint32_t seed = 0x9e3779b9u)
{
    std::vector<float> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        out[static_cast<std::size_t>(i)] =
            amplitude * static_cast<float>(static_cast<std::int32_t>(seed)) * 4.6566129e-10f;
    }
    return out;
}

/// The transform costs one window of latency, and the element must declare it
/// so the engine can tell the host.
void testLatencyIsOneWindow()
{
    ElementTestFixture rig("SpectralGate", kRate);
    assert(rig.element->latencyInSamples() == kWindow);

    std::string error;
    assert(rig.element->setOption("fftSize", "2048", error));
    assert(rig.element->latencyInSamples() == 2048);
}

/// With the threshold below everything, no bin is removed, so what comes out is
/// what went in. This is the overlap-add identity: analysis window times
/// synthesis window, summed at the hop, has to come to one.
void testUntouchedFramesResynthesiseTheInput()
{
    ElementTestFixture rig("SpectralGate", kRate);
    rig.set("threshold", -120.0f);

    const int n = 4096;
    const auto input = exactTone(85, n, 0.5f);   // 996.09 Hz
    const auto out   = settled(rig, input);

    const float in  = ElementTestFixture::measureRms(input);
    const float got = ElementTestFixture::measureRms(out);

    std::printf("  spectral passthrough: in rms %.4f, out rms %.4f\n", in, got);
    std::fflush(stdout);

    // Within 2%: the overlap-add reconstructs, it does not merely approximate.
    assert(std::abs(got - in) < in * 0.02f);
}

/// Quiet broadband noise is below the threshold in every bin, so all of it goes.
void testQuietNoiseIsRemoved()
{
    ElementTestFixture rig("SpectralGate", kRate);
    rig.set("threshold", -40.0f);

    const int n = 4096;
    const auto input = noise(n, 0.01f);        // about -46 dBFS peak
    const auto out   = settled(rig, input);

    const float in  = ElementTestFixture::measureRms(input);
    const float got = ElementTestFixture::measureRms(out);

    std::printf("  spectral gate on noise: in rms %.5f, out rms %.5f\n", in, got);
    std::fflush(stdout);

    assert(got < in * 0.1f);
}

/// The point of doing this in the frequency domain: a loud tone survives while
/// the quiet noise underneath it is removed. No time-domain filter can separate
/// those, because they occupy the same moments.
void testLoudToneSurvivesWhileNoiseUnderItGoes()
{
    const int n = 4096;
    const int toneCycles = 85;                   // 996.09 Hz, a whole number of
    const auto tone  = exactTone(toneCycles, n, 0.5f);   // cycles in the block
    const auto floor = noise(n, 0.01f);

    std::vector<float> mixed(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        mixed[static_cast<std::size_t>(i)] =
            tone[static_cast<std::size_t>(i)] + floor[static_cast<std::size_t>(i)];

    ElementTestFixture rig("SpectralGate", kRate);
    rig.set("threshold", -40.0f);
    const auto out = settled(rig, mixed);

    const auto spectrumOf = [](const std::vector<float>& signal)
    {
        return ElementTestFixture::computeSpectrum(signal, 12);
    };

    const auto before = spectrumOf(mixed);
    const auto after  = spectrumOf(out);

    // The tone sits exactly on this bin, by construction.
    const auto toneBin = static_cast<std::size_t>(toneCycles);

    // Energy away from the tone, which is the noise floor.
    const auto floorEnergy = [&](const std::vector<float>& bins)
    {
        double total = 0.0;
        for (std::size_t b = 1; b < bins.size(); ++b)
            if (b < toneBin - 8 || b > toneBin + 8)
                total += static_cast<double>(bins[b]) * bins[b];
        return total;
    };

    const double toneBefore = before[toneBin];
    const double toneAfter  = after[toneBin];
    const double noiseBefore = floorEnergy(before);
    const double noiseAfter  = floorEnergy(after);

    std::printf("  spectral gate: tone %.1f -> %.1f, noise energy %.3e -> %.3e\n",
                toneBefore, toneAfter, noiseBefore, noiseAfter);
    std::fflush(stdout);

    // The tone is essentially untouched.
    assert(toneAfter > toneBefore * 0.8f);

    // The noise under it is mostly gone.
    assert(noiseAfter < noiseBefore * 0.25);
}

/// A window that is not a power of two, or is out of range, is a located load
/// failure rather than a silently ignored option.
void testFftSizeIsValidated()
{
    ElementTestFixture rig("SpectralGate", kRate);

    std::string error;
    assert(rig.element->setOption("fftSize", "2048", error));
    assert(error.empty());

    assert(! rig.element->setOption("fftSize", "1000", error));
    assert(error.find("power of two") != std::string::npos);

    error.clear();
    assert(! rig.element->setOption("fftSize", "16384", error));
    assert(! error.empty());

    // An unrecognised key is not a failure.
    std::string ignored;
    assert(rig.element->setOption("nonsense", "1", ignored));
    assert(ignored.empty());
}

/// The transform runs on the sample stream, so the host's buffer size must not
/// change what comes out.
void testBlockSizeIndependence()
{
    const int n = 8192;
    const auto input = ElementTestFixture::generateSine(700.0, kRate, n, 0.4f);

    const auto renderIn = [&](int blockSize)
    {
        ElementTestFixture rig("SpectralGate", kRate);
        rig.set("threshold", -120.0f);

        std::vector<float> out;
        out.reserve(static_cast<std::size_t>(n));
        for (int at = 0; at < n; at += blockSize)
        {
            const int count = std::min(blockSize, n - at);
            const std::vector<float> chunk(input.begin() + at, input.begin() + at + count);
            const auto piece = rig.run(chunk);
            out.insert(out.end(), piece.begin(), piece.end());
        }
        return out;
    };

    const auto a = renderIn(512);
    const auto b = renderIn(333);

    assert(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        assert(std::abs(a[i] - b[i]) < 1.0e-5f);
}

}  // namespace

int main()
{
    testLatencyIsOneWindow();
    testUntouchedFramesResynthesiseTheInput();
    testQuietNoiseIsRemoved();
    testLoudToneSurvivesWhileNoiseUnderItGoes();
    testFftSizeIsValidated();
    testBlockSizeIndependence();

    std::puts("SpectralTest PASSED");
    return 0;
}

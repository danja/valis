// tests/dsp/SpectralExtrasTest.cpp
//
// val:SpectralDelay, val:Codec and val:LPC, the elements built on the same
// block machinery as val:SpectralGate (see SpectralTest.cpp for the transform
// itself). Each is judged by one thing it exists to do.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

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

/// A short sine burst with a smooth envelope, starting at sample 0.
std::vector<float> burst(double hz, int length, int total)
{
    std::vector<float> out(static_cast<std::size_t>(total), 0.0f);
    for (int i = 0; i < length; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / length);
        out[static_cast<std::size_t>(i)] =
            static_cast<float>(window * std::sin(2.0 * M_PI * hz * i / kRate));
    }
    return out;
}

/// The sample where the signal's energy is centred.
double centroid(const std::vector<float>& signal)
{
    double sum = 0.0, weighted = 0.0;
    for (std::size_t i = 0; i < signal.size(); ++i)
    {
        const double e = static_cast<double>(signal[i]) * signal[i];
        sum += e;
        weighted += e * static_cast<double>(i);
    }
    return sum > 0.0 ? weighted / sum : 0.0;
}

double bandPower(const std::vector<float>& signal, double lo, double hi, int order);

float levelAt(const std::vector<float>& signal, double hz, int order)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, order);
    const int size = 1 << order;
    const auto centre = static_cast<std::size_t>(std::lround(hz * size / kRate));
    float best = 0.0f;
    for (std::size_t b = centre - 2; b <= centre + 2; ++b)
        best = std::max(best, bins[b]);
    return best;
}

// --- SpectralDelay -------------------------------------------------------

void testSpectralDelayPlainDelay()
{
    ElementTestFixture rig("SpectralDelay", kRate);
    rig.set("time", 50.0f);
    rig.set("spread", 0.0f);
    rig.set("feedback", 0.0f);
    rig.set("mix", 1.0f);

    const int latency = rig.element->latencyInSamples();
    assert(latency > 0);

    const int total = 16384;
    const auto out = rig.run(burst(2000.0, 256, total));
    const double expected = latency + 0.050 * kRate;   // a hop of rounding either way
    const double at = centroid(out);
    std::printf("  50 ms plain delay: energy centred at %.0f, expected about %.0f\n", at, expected + 128);
    assert(std::abs(at - (expected + 128.0)) < 300.0);
}

void testSpectralDelaySpreadsTheSpectrum()
{
    const auto arrival = [](double hz, float spread)
    {
        ElementTestFixture rig("SpectralDelay", kRate);
        rig.set("time", 100.0f);
        rig.set("spread", spread);
        rig.set("feedback", 0.0f);
        rig.set("mix", 1.0f);
        return centroid(rig.run(burst(hz, 256, 32768)));
    };

    const double lowFirst  = arrival(8000.0, 1.0f) - arrival(500.0, 1.0f);
    const double highFirst = arrival(8000.0, -1.0f) - arrival(500.0, -1.0f);
    std::printf("  spread +1: 8 kHz arrives %.1f ms after 500 Hz; spread -1: %.1f ms\n",
                lowFirst * 1000.0 / kRate, highFirst * 1000.0 / kRate);
    assert(lowFirst  >  0.030 * kRate);
    assert(highFirst < -0.030 * kRate);
}

void testSpectralDelayFeedbackRepeatsAndDecays()
{
    ElementTestFixture rig("SpectralDelay", kRate);
    rig.set("time", 100.0f);
    rig.set("spread", 0.0f);
    rig.set("feedback", 0.5f);
    rig.set("mix", 1.0f);

    const auto out = rig.run(burst(1500.0, 256, 48000));
    const auto energy = [&](double fromMs, double toMs)
    {
        double e = 0.0;
        for (auto i = static_cast<std::size_t>(fromMs * 48.0); i < static_cast<std::size_t>(toMs * 48.0); ++i)
            e += static_cast<double>(out[i]) * out[i];
        return e;
    };

    // First arrival near 121 ms (latency 21 ms plus 100), the repeat 100 ms later, quieter.
    const double first = energy(110.0, 150.0), second = energy(210.0, 250.0), third = energy(310.0, 350.0);
    std::printf("  repeats: %.4f, %.4f, %.4f\n", first, second, third);
    assert(first > 0.0 && second > 0.1 * first && second < 0.5 * first);
    assert(third < second);
}

// --- Codec ---------------------------------------------------------------

void testCodecNeutralIsADelay()
{
    ElementTestFixture rig("Codec", kRate);
    rig.set("bandwidth", 24000.0f);
    rig.set("step", 0.0f);
    rig.set("floor", 0.0f);

    const int latency = rig.element->latencyInSamples();
    const auto in = noise(8192, 0.3f);
    const auto out = rig.run(in);
    for (std::size_t i = static_cast<std::size_t>(latency) + 8; i < in.size(); ++i)
        assert(std::abs(out[i] - in[i - static_cast<std::size_t>(latency)]) < 1.0e-3f);
}

void testCodecBandwidthRemovesTheTop()
{
    ElementTestFixture rig("Codec", kRate);
    rig.set("bandwidth", 5000.0f);
    rig.set("step", 0.0f);
    rig.set("floor", 0.0f);

    const auto in = ElementTestFixture::generateSine(48000.0 * 2048 / 16384, kRate, 16384, 0.5f);   // 6 kHz
    auto low = ElementTestFixture::generateSine(48000.0 * 512 / 16384, kRate, 16384, 0.5f);          // 1.5 kHz
    std::vector<float> both(in.size());
    for (std::size_t i = 0; i < both.size(); ++i) both[i] = in[i] + low[i];

    const auto out = rig.run(both);
    const std::vector<float> tail(out.end() - 8192, out.end());
    const float lowLevel  = levelAt(tail, 1500.0, 13);
    const float highLevel = levelAt(tail, 6000.0, 13);
    const double db = 20.0 * std::log10(lowLevel / std::max(highLevel, 1.0e-12f));
    std::printf("  bandwidth 5 kHz: 1.5 kHz is %.1f dB above 6 kHz\n", db);
    assert(db > 40.0);
}

void testCodecFloorDropsQuietBins()
{
    const auto run = [](float floorDb)
    {
        ElementTestFixture rig("Codec", kRate);
        rig.set("bandwidth", 24000.0f);
        rig.set("step", 0.0f);
        rig.set("floor", floorDb);

        auto tone = ElementTestFixture::generateSine(48000.0 * 256 / 16384, kRate, 16384, 0.5f);
        const auto hiss = noise(16384, 0.02f);
        for (std::size_t i = 0; i < tone.size(); ++i) tone[i] += hiss[i];

        const auto out = rig.run(tone);
        const std::vector<float> tail(out.end() - 8192, out.end());
        return std::pair{ levelAt(tail, 750.0, 13), bandPower(tail, 4000.0, 20000.0, 13) };
    };

    const auto [keptTone, keptHiss]       = run(0.0f);
    const auto [droppedTone, droppedHiss] = run(40.0f);
    const double db = 10.0 * std::log10(keptHiss / std::max(droppedHiss, 1.0e-20));
    std::printf("  floor 40 dB removes %.1f dB of the hiss and leaves the tone at %.2f of its level\n",
                db, droppedTone / keptTone);
    assert(db > 15.0);
    assert(droppedTone > 0.9f * keptTone);
}

// --- LPC -----------------------------------------------------------------

/// Noise through a two-pole resonator at `hz`: a source with one clear formant.
std::vector<float> resonantNoise(double hz, int n)
{
    const auto source = noise(n, 0.3f, 12345u);
    const double r = 0.97, w = 2.0 * M_PI * hz / kRate;
    const double a1 = -2.0 * r * std::cos(w), a2 = r * r;

    std::vector<float> out(static_cast<std::size_t>(n));
    double y1 = 0.0, y2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double y = source[static_cast<std::size_t>(i)] - a1 * y1 - a2 * y2;
        out[static_cast<std::size_t>(i)] = static_cast<float>(y * 0.05);
        y2 = y1; y1 = y;
    }
    return out;
}

/// Mean of the spectrum's power from `lo` to `hi` Hz.
double bandPower(const std::vector<float>& signal, double lo, double hi, int order)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, order);
    const int size = 1 << order;
    double sum = 0.0;
    int count = 0;
    for (std::size_t b = 1; b < bins.size(); ++b)
    {
        const double hz = static_cast<double>(b) * kRate / size;
        if (hz >= lo && hz <= hi)
        {
            sum += static_cast<double>(bins[b]) * bins[b];
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

void testLpcImposesTheModulatorsFormant()
{
    const int n = 1 << 15;
    const auto modulator = resonantNoise(1500.0, n);
    const auto carrier   = noise(n, 0.3f, 777u);   // white: no formant of its own

    ElementTestFixture rig("LPC", kRate);
    const auto out = rig.run(modulator, "out", &carrier);
    const std::vector<float> tail(out.end() - 16384, out.end());

    const double near = bandPower(tail, 1300.0, 1700.0, 14);
    const double far  = bandPower(tail, 6000.0, 8000.0, 14);
    const double db = 10.0 * std::log10(near / far);
    std::printf("  white carrier shaped by a 1.5 kHz formant: %.1f dB above 6-8 kHz\n", db);
    assert(db > 15.0);

    // The carrier alone has no such peak.
    const std::vector<float> plain(carrier.end() - 16384, carrier.end());
    const double plainDb = 10.0 * std::log10(bandPower(plain, 1300.0, 1700.0, 14)
                                             / bandPower(plain, 6000.0, 8000.0, 14));
    assert(std::abs(plainDb) < 3.0);
}

void testLpcWhitensTheCarrier()
{
    // A carrier that is itself coloured gives the same result as a white one:
    // the modulator's envelope is what is heard.
    const int n = 1 << 15;
    const auto modulator = resonantNoise(1500.0, n);
    const auto coloured  = resonantNoise(6500.0, n);   // its own formant, somewhere else

    ElementTestFixture rig("LPC", kRate);
    const auto out = rig.run(modulator, "out", &coloured);
    const std::vector<float> tail(out.end() - 16384, out.end());

    const double near = bandPower(tail, 1300.0, 1700.0, 14);
    const double carriersOwn = bandPower(tail, 6200.0, 6800.0, 14);
    const double db = 10.0 * std::log10(near / carriersOwn);
    std::printf("  coloured carrier: modulator's formant is %.1f dB above the carrier's own\n", db);
    assert(db > 6.0);
}

void testLpcStaysFiniteAndSilentOnSilence()
{
    ElementTestFixture rig("LPC", kRate);
    const std::vector<float> silence(4096, 0.0f);
    const auto carrier = noise(4096, 0.5f);
    const auto out = rig.run(silence, "out", &carrier);
    for (const float v : out)
        assert(std::isfinite(v) && std::abs(v) < 4.0f);

    // Loud input in both, a stress for the recursion.
    const auto loud = noise(8192, 1.0f, 99u);
    const auto out2 = rig.run(loud, "out", &loud);
    for (const float v : out2)
        assert(std::isfinite(v) && std::abs(v) < 50.0f);
}

}  // namespace

int main()
{
    testSpectralDelayPlainDelay();
    testSpectralDelaySpreadsTheSpectrum();
    testSpectralDelayFeedbackRepeatsAndDecays();
    testCodecNeutralIsADelay();
    testCodecBandwidthRemovesTheTop();
    testCodecFloorDropsQuietBins();
    testLpcImposesTheModulatorsFormant();
    testLpcWhitensTheCarrier();
    testLpcStaysFiniteAndSilentOnSilence();

    std::printf("SpectralExtrasTest PASSED\n");
    return 0;
}

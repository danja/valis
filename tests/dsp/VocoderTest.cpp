// tests/dsp/VocoderTest.cpp
//
// val:Vocoder. A vocoder is only worth having if the carrier takes the modulator's spectrum: a modulator that
// is a tone in one band must leave the carrier's energy in that band and silence the rest, nothing comes out
// when the modulator is silent, the output follows the modulator's loudness, and the bands do not move with
// the sample rate.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr int kSize = 1 << 14;

/// A sawtooth, which has a harmonic in every band.
std::vector<float> sawtooth(double hz, double rate, int n)
{
    std::vector<float> out(static_cast<std::size_t>(n));
    double phase = 0.0;
    for (auto& v : out)
    {
        v = static_cast<float>(2.0 * phase - 1.0) * 0.5f;
        phase += hz / rate;
        if (phase >= 1.0) phase -= 1.0;
    }
    return out;
}

std::vector<float> vocode(double rate, const std::vector<float>& modulator, const std::vector<float>& carrier)
{
    ElementTestFixture rig("Vocoder", rate);
    const auto out = rig.run(modulator, "out", &carrier);
    return std::vector<float>(out.end() - kSize, out.end());
}

double rms(const std::vector<float>& x)
{
    double sum = 0.0;
    for (const float v : x) sum += static_cast<double>(v) * v;
    return std::sqrt(sum / static_cast<double>(x.size()));
}

/// Power between two frequencies.
double bandPower(const std::vector<float>& signal, double rate, double from, double to)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, 14);
    double sum = 0.0;
    for (std::size_t b = 1; b < bins.size(); ++b)
    {
        const double hz = static_cast<double>(b) * rate / kSize;
        if (hz >= from && hz <= to)
            sum += static_cast<double>(bins[b]) * bins[b];
    }
    return sum;
}

void testCarrierTakesTheModulatorsBand()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const int n = static_cast<int>(rate);
        const auto carrier = sawtooth(110.0, rate, n);
        const auto low  = vocode(rate, ElementTestFixture::generateSine(400.0,  rate, n, 0.5f), carrier);
        const auto high = vocode(rate, ElementTestFixture::generateSine(3000.0, rate, n, 0.5f), carrier);

        const double lowIn  = bandPower(low,  rate, 250.0, 650.0);
        const double lowOut = bandPower(low,  rate, 1800.0, 6000.0);
        const double hiIn   = bandPower(high, rate, 2000.0, 4500.0);
        const double hiOut  = bandPower(high, rate, 100.0, 700.0);
        std::printf("rate %.0f: 400 Hz modulator in/out %.3g/%.3g, 3 kHz modulator in/out %.3g/%.3g\n",
                    rate, lowIn, lowOut, hiIn, hiOut);
        assert(lowIn  > 20.0 * lowOut);
        assert(hiIn   > 20.0 * hiOut);
    }
}

void testSilentModulatorGivesSilence()
{
    const double rate = 48000.0;
    const auto out = vocode(rate, std::vector<float>(48000, 0.0f), sawtooth(110.0, rate, 48000));
    assert(rms(out) < 1.0e-6);
}

void testLevelFollowsTheModulator()
{
    const double rate = 48000.0;
    const auto carrier = sawtooth(110.0, rate, 48000);
    const double loud  = rms(vocode(rate, ElementTestFixture::generateSine(1000.0, rate, 48000, 0.5f),  carrier));
    const double quiet = rms(vocode(rate, ElementTestFixture::generateSine(1000.0, rate, 48000, 0.05f), carrier));
    std::printf("loud %.5f quiet %.5f ratio %.2f\n", loud, quiet, loud / quiet);
    assert(loud > 0.001);
    assert(std::abs(loud / quiet - 10.0) < 1.5);
}

void testMixZeroPassesTheCarrier()
{
    const double rate = 48000.0;
    ElementTestFixture rig("Vocoder", rate);
    rig.set("mix", 0.0f);
    const auto carrier = sawtooth(110.0, rate, 4800);
    const auto out = rig.run(ElementTestFixture::generateSine(1000.0, rate, 4800), "out", &carrier);
    for (std::size_t i = 0; i < out.size(); ++i)
        assert(std::abs(out[i] - carrier[i]) < 1.0e-6f);
}

void testNoCarrierPassesTheInput()
{
    const double rate = 48000.0;
    ElementTestFixture rig("Vocoder", rate);
    const auto in = ElementTestFixture::generateSine(1000.0, rate, 256);
    const auto out = rig.run(in);
    for (std::size_t i = 0; i < out.size(); ++i)
        assert(std::isfinite(out[i]));
}

}  // namespace

int main()
{
    testCarrierTakesTheModulatorsBand();
    testSilentModulatorGivesSilence();
    testLevelFollowsTheModulator();
    testMixZeroPassesTheCarrier();
    testNoCarrierPassesTheInput();
    std::puts("VocoderTest passed");
    return 0;
}

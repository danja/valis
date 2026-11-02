// tests/engine/VocoderSpeakTest.cpp
//
// circuits/vocoder-speak.ttl (scripts/elements-demo/vocoder-speak.py): a note on MIDI shaped by a voice on the audio input.
// The voice here is made, not recorded: a pulse train through three resonators set to the formants of "ah" and of "ee".
// With the note held, the output must take on the vowel it is given, by either method: the second formant of "ee"
// lies near 2.3 kHz and the first of "ah" near 730 Hz, so the balance between those two bands is what moves. The plain
// carrier does not move it, which shows it is the voice that does it. The circuit is silent with no note and with no voice.

#include "ExampleRig.h"
#include "Fft.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

using valis::test::ExampleRig;

namespace {

constexpr double kRate = ExampleRig::kRate;

/// A vowel: impulses at `f0` through parallel two-pole resonators at the given formants (frequency, bandwidth, weight).
std::vector<float> vowel(double seconds, double f0, const std::vector<std::array<double, 3>>& formants)
{
    const auto n = static_cast<std::size_t>(seconds * kRate);
    std::vector<float> out(n, 0.0f);
    for (const auto& [freq, bandwidth, weight] : formants)
    {
        const double r = std::exp(-std::numbers::pi * bandwidth / kRate);
        const double a1 = 2.0 * r * std::cos(2.0 * std::numbers::pi * freq / kRate), a2 = -r * r;
        double y1 = 0.0, y2 = 0.0, nextImpulse = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            double x = 0.0;
            if (static_cast<double>(i) >= nextImpulse)
            {
                x = 1.0;
                nextImpulse += kRate / f0;
            }
            const double y = (1.0 - r) * x + a1 * y1 + a2 * y2;
            y2 = y1;
            y1 = y;
            out[i] += static_cast<float>(weight * y);
        }
    }
    double sum = 0.0;
    for (const float v : out)
        sum += static_cast<double>(v) * v;
    const double gain = 0.1 / std::sqrt(sum / static_cast<double>(n));
    for (auto& v : out)
        v = static_cast<float>(v * gain);
    return out;
}

std::vector<float> ah() { return vowel(1.6, 110.0, {{730.0, 90.0, 1.0}, {1090.0, 110.0, 0.7}, {2440.0, 160.0, 0.3}}); }
std::vector<float> ee() { return vowel(1.6, 110.0, {{270.0, 60.0, 1.0}, {2290.0, 150.0, 0.8}, {3010.0, 200.0, 0.4}}); }

/// The circuit's output while a note is held and `voice` is on the input.
std::vector<float> speak(const std::vector<float>& voice, float method, bool note = true)
{
    ExampleRig rig("vocoder-speak.ttl");
    rig.setParam("method", method);
    if (note)
        rig.engine.noteOn(48, 1.0f);
    std::vector<float> out, left(ExampleRig::kBlock), right(ExampleRig::kBlock);
    for (std::size_t at = 0; at + ExampleRig::kBlock <= voice.size(); at += ExampleRig::kBlock)
    {
        rig.engine.process(voice.data() + at, left.data(), right.data(), ExampleRig::kBlock);
        out.insert(out.end(), left.begin(), left.end());
    }
    for (const float v : out)
        assert(std::isfinite(v));
    return out;
}

double bandDb(const std::vector<float>& x, double lo, double hi)
{
    const std::size_t n = 65536, start = static_cast<std::size_t>(0.4 * kRate), len = static_cast<std::size_t>(1.0 * kRate);
    std::vector<std::complex<double>> a(n, 0.0);
    for (std::size_t i = 0; i < len; ++i)
        a[i] = x[start + i] * (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(len - 1)));
    valis::test::fft(a);
    double sum = 0.0;
    for (std::size_t k = 0; k < n / 2; ++k)
    {
        const double f = static_cast<double>(k) * kRate / static_cast<double>(n);
        if (f >= lo && f < hi)
            sum += std::norm(a[k]);
    }
    return 10.0 * std::log10(sum + 1e-20);
}

/// Energy around the second formant of "ee" over energy around the first formant of "ah", in decibels.
double balance(const std::vector<float>& x) { return bandDb(x, 2000, 2600) - bandDb(x, 600, 900); }

double rms(const std::vector<float>& x)
{
    double sum = 0.0;
    for (const float v : x)
        sum += static_cast<double>(v) * v;
    return std::sqrt(sum / static_cast<double>(x.size()));
}

}  // namespace

int main()
{
    const auto vowelAh = ah(), vowelEe = ee();
    const auto none = std::vector<float>(vowelAh.size(), 0.0f);

    // silent with no note, and with no voice
    assert(rms(speak(vowelAh, 0.0f, false)) < 1e-4);
    assert(rms(speak(none, 0.0f)) < 1e-4);
    assert(rms(speak(none, 1.0f)) < 1e-4);

    // each method takes on the vowel it is given
    const double lpcShift = balance(speak(vowelEe, 0.0f)) - balance(speak(vowelAh, 0.0f));
    const double vocShift = balance(speak(vowelEe, 1.0f)) - balance(speak(vowelAh, 1.0f));
    const double plainShift = balance(speak(vowelEe, 2.0f)) - balance(speak(vowelAh, 2.0f));
    std::fprintf(stderr, "ee against ah, in dB: LPC %.1f, vocoder %.1f, plain carrier %.1f\n", lpcShift, vocShift, plainShift);
    assert(lpcShift > 10.0);
    assert(vocShift > 10.0);
    assert(std::abs(plainShift) < 1.0);

    // the output is a sound of reasonable level, not a whisper or a clip, and the two methods are about as loud as each other
    double level[2];
    for (const int method : {0, 1})
    {
        const auto y = speak(vowelAh, static_cast<float>(method));
        level[method] = 20.0 * std::log10(rms(y));
        assert(rms(y) > 0.02);
        assert(*std::max_element(y.begin(), y.end()) <= 1.0f && *std::min_element(y.begin(), y.end()) >= -1.0f);
    }
    assert(std::abs(level[0] - level[1]) < 3.0);

    std::puts("VocoderSpeakTest passed");
    return 0;
}

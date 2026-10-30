// tests/ops/AnalysisTest.cpp
//
// valis::analyseSignal, the measurement a caller uses in place of listening. Each test
// builds a signal whose answer is known and checks the report says so.

#include "valis/Analysis.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(double hz, double rate, double seconds, float amplitude = 0.5f)
{
    std::vector<float> out(static_cast<std::size_t>(rate * seconds));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = amplitude * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
    return out;
}

bool said(const SignalReport& r, const char* fragment)
{
    return std::any_of(r.notes.begin(), r.notes.end(),
                       [&](const std::string& n) { return n.find(fragment) != std::string::npos; });
}

void testASineIsMeasuredExactly()
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
    {
        const auto r = analyseSignal(sine(440.0, rate, 2.0), {}, rate);
        std::printf("  440 Hz at %.0f: pitch %.2f Hz %s %+.1f cents, peak %.1f dB, rms %.1f dB, centroid %.0f\n",
                    rate, r.pitch.hz, noteName(r.pitch.midiNote).c_str(), r.pitch.cents, 20 * std::log10(r.peak),
                    20 * std::log10(r.rms), r.centroidHz);

        assert(r.pitch.found);
        assert(std::abs(r.pitch.hz - 440.0) < 1.0);
        assert(r.pitch.midiNote == 69 && noteName(69) == "A4");
        assert(std::abs(r.pitch.cents) < 5.0);
        assert(r.pitch.stability > 0.95);
        assert(std::abs(r.peak - 0.5) < 1.0e-3);
        assert(std::abs(r.rms - 0.5 / std::sqrt(2.0)) < 1.0e-3);
        assert(std::abs(r.centroidHz - 440.0) < 30.0);
        assert(r.clipped == 0 && r.nonFinite == 0);
        assert(std::abs(r.dc) < 1.0e-3);
        assert(r.seconds > 1.99 && r.seconds < 2.01);
    }
}

void testPitchAcrossTheRange()
{
    for (const double hz : {55.0, 110.0, 261.63, 1000.0, 1400.0})
    {
        const auto r = analyseSignal(sine(hz, 48000.0, 2.0), {}, 48000.0);
        const double cents = 1200.0 * std::log2(r.pitch.hz / hz);
        std::printf("  %.2f Hz: found %.2f Hz (%.1f cents)\n", hz, r.pitch.hz, cents);
        assert(r.pitch.found && std::abs(cents) < 10.0);
    }

    // Harmonic-rich sounds give the fundamental, not a partial.
    std::vector<float> saw(96000);
    for (std::size_t i = 0; i < saw.size(); ++i)
        saw[i] = 0.4f * (2.0f * static_cast<float>(std::fmod(196.0 * static_cast<double>(i) / 48000.0, 1.0)) - 1.0f);
    const auto r = analyseSignal(saw, {}, 48000.0);
    assert(r.pitch.found && std::abs(r.pitch.hz - 196.0) < 2.0);
}

void testAWeakFundamentalIsNotAnOctaveHigh()
{
    // A plucked nylon string played as E3 has a strong second harmonic: the half-period dip in the difference function
    // falls under the usual threshold, though the true period dips far lower. The estimate must be E3, not E4.
    constexpr double rate = 48000.0, f0 = 164.81;
    std::vector<float> x(static_cast<std::size_t>(rate * 2.0));
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = 2.0 * kPi * f0 * static_cast<double>(i) / rate;
        x[i] = static_cast<float>(0.08 * std::sin(t) + 0.5 * std::sin(2.0 * t) + 0.03 * std::sin(3.0 * t));
    }
    const auto r = analyseSignal(x, {}, rate);
    std::printf("  weak fundamental: found %.2f Hz (%s)\n", r.pitch.hz, noteName(r.pitch.midiNote).c_str());
    assert(r.pitch.found && std::abs(1200.0 * std::log2(r.pitch.hz / f0)) < 10.0);
    assert(noteName(r.pitch.midiNote) == "E3");
}

void testBandsAreInDecibelsFullScale()
{
    // A 1 kHz sine at 0.5 puts all of its power in the 1 kHz octave: -9 dBFS RMS.
    const auto r = analyseSignal(sine(1000.0, 48000.0, 2.0), {}, 48000.0);
    assert(r.bandsDb.size() == 9);
    assert(std::abs(r.bandsDb[4] - (20 * std::log10(0.5 / std::sqrt(2.0)))) < 0.5);
    for (std::size_t b = 0; b < r.bandsDb.size(); ++b)
        if (b != 4)
            assert(r.bandsDb[b] < r.bandsDb[4] - 40.0);
    assert(std::abs(r.centroidHz - 1000.0) < 60.0);
    assert(r.rolloffHz > 900.0 && r.rolloffHz < 1200.0);
}

void testSilenceIsSaidToBeSilence()
{
    const auto r = analyseSignal(std::vector<float>(48000, 0.0f), {}, 48000.0);
    assert(r.peak == 0.0 && r.silentFraction > 0.99);
    assert(said(r, "Silent"));
    assert(! r.pitch.found);
    assert(r.firstSoundAt < 0.0);
}

void testClippingDcAndBrokenValuesAreFlagged()
{
    auto loud = sine(200.0, 48000.0, 1.0, 1.6f);
    for (auto& s : loud) s = std::clamp(s, -1.0f, 1.0f);
    auto r = analyseSignal(loud, {}, 48000.0);
    assert(r.clipped > 1000 && said(r, "Clips"));

    std::vector<float> offset(48000, 0.2f);
    for (std::size_t i = 0; i < offset.size(); ++i) offset[i] += 0.1f * static_cast<float>(std::sin(0.05 * static_cast<double>(i)));
    r = analyseSignal(offset, {}, 48000.0);
    assert(std::abs(r.dc - 0.2) < 0.01 && said(r, "DC offset"));

    auto broken = sine(440.0, 48000.0, 1.0);
    broken[1000] = std::nanf("");
    broken[2000] = INFINITY;
    r = analyseSignal(broken, {}, 48000.0);
    assert(r.nonFinite == 2 && said(r, "not numbers"));
    assert(r.pitch.found && std::abs(r.pitch.hz - 440.0) < 2.0);   // one bad sample does not spoil the rest

    r = analyseSignal(sine(440.0, 48000.0, 1.0, 0.0001f), {}, 48000.0);
    assert(said(r, "Very quiet"));
}

void testNoiseHasNoPitchAndIsBright()
{
    std::vector<float> noise(96000);
    std::uint32_t state = 7;
    for (auto& v : noise)
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        v = 0.3f * static_cast<float>(static_cast<std::int32_t>(state)) * 4.6566129e-10f;
    }
    const auto r = analyseSignal(noise, {}, 48000.0);
    assert(! r.pitch.found && said(r, "No clear pitch"));
    assert(r.centroidHz > 5000.0);
}

void testOnsetsAreFoundAtTheirTimes()
{
    std::vector<float> clicks(static_cast<std::size_t>(48000 * 3), 0.0f);
    for (const double t : {0.5, 1.2, 2.0})
        for (int i = 0; i < 2400; ++i)
        {
            const auto at = static_cast<std::size_t>(t * 48000.0) + static_cast<std::size_t>(i);
            clicks[at] = 0.6f * std::exp(-static_cast<float>(i) / 600.0f)
                       * static_cast<float>(std::sin(2.0 * kPi * 300.0 * i / 48000.0));
        }

    const auto r = analyseSignal(clicks, {}, 48000.0);
    std::printf("  onsets at:");
    for (const double t : r.onsets) std::printf(" %.2f", t);
    std::printf("\n");
    assert(r.onsets.size() == 3);
    assert(std::abs(r.onsets[0] - 0.5) < 0.03 && std::abs(r.onsets[1] - 1.2) < 0.03 && std::abs(r.onsets[2] - 2.0) < 0.03);
    assert(std::abs(r.firstSoundAt - 0.5) < 0.06);

    // The envelope falls away after each hit.
    assert(r.envelopeDb.size() == 12);
    assert(r.envelopeDb[2] > r.envelopeDb[4]);   // 0.5-0.75 s against 1.0-1.25 s's quiet start
}

void testALateStartIsNoted()
{
    std::vector<float> late(96000, 0.0f);
    const auto tone = sine(440.0, 48000.0, 1.0);
    std::copy(tone.begin(), tone.end(), late.begin() + 48000);
    const auto r = analyseSignal(late, {}, 48000.0);
    assert(r.firstSoundAt > 0.9 && said(r, "Nothing sounds for the first"));
}

void testStereo()
{
    const auto tone = sine(330.0, 48000.0, 1.0);
    auto flipped = tone;
    for (auto& s : flipped) s = -s;

    auto r = analyseSignal(tone, tone, 48000.0);
    assert(r.channels == 2 && r.stereoCorrelation > 0.999 && std::abs(r.balanceDb) < 0.01);

    r = analyseSignal(tone, flipped, 48000.0);
    assert(r.stereoCorrelation < -0.999 && said(r, "opposite in phase"));

    auto quieter = tone;
    for (auto& s : quieter) s *= 0.5f;
    r = analyseSignal(tone, quieter, 48000.0);
    assert(std::abs(r.balanceDb + 6.02) < 0.1);
}

void testTheJsonIsWellFormedEnough()
{
    const auto r = analyseSignal(sine(440.0, 48000.0, 1.0), {}, 48000.0);
    const auto json = reportToJson(r);

    // Balanced braces and brackets, and the fields a caller reads.
    int braces = 0, brackets = 0;
    for (const char c : json)
    {
        braces   += c == '{' ? 1 : (c == '}' ? -1 : 0);
        brackets += c == '[' ? 1 : (c == ']' ? -1 : 0);
        assert(braces >= 0 && brackets >= 0);
    }
    assert(braces == 0 && brackets == 0);
    for (const char* field : {"\"pitch\"", "\"hz\"", "\"note\":\"A4\"", "\"bandsDb\"", "\"centroidHz\"",
                              "\"onsets\"", "\"notes\"", "\"peakDb\""})
        assert(json.find(field) != std::string::npos);
    assert(json.find("nan") == std::string::npos && json.find("inf") == std::string::npos);
}

}  // namespace

int main()
{
    testASineIsMeasuredExactly();
    testPitchAcrossTheRange();
    testAWeakFundamentalIsNotAnOctaveHigh();
    testBandsAreInDecibelsFullScale();
    testSilenceIsSaidToBeSilence();
    testClippingDcAndBrokenValuesAreFlagged();
    testNoiseHasNoPitchAndIsBright();
    testOnsetsAreFoundAtTheirTimes();
    testALateStartIsNoted();
    testStereo();
    testTheJsonIsWellFormedEnough();

    std::puts("AnalysisTest PASSED");
    return 0;
}

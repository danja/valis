// tests/dsp/TimeStretchTest.cpp
//
// val:TimeStretch. A stretcher is judged by what it keeps and what it moves: the
// pitch must stay where it was when only the speed changes, the duration must stay
// where it was when only the pitch changes, and a voice moved up must keep its vowel
// when the formants are held. The sources are written to the temporary directory so
// the test depends on no shipped sample.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi   = 3.14159265358979323846;

/// A mono 16-bit WAV at the engine's rate.
std::string writeWav(const std::string& name, const std::vector<float>& samples)
{
    const auto path = (std::filesystem::temp_directory_path() / ("valis_timestretch_" + name + ".wav")).string();
    std::ofstream file(path, std::ios::binary);

    const auto put32 = [&](std::uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](std::uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);

    file.write("RIFF", 4); put32(36 + bytes); file.write("WAVE", 4);
    file.write("fmt ", 4); put32(16); put16(1); put16(1); put32(48000); put32(96000); put16(2); put16(16);
    file.write("data", 4); put32(bytes);
    for (const float s : samples)
        put16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f))));
    return path;
}

std::vector<float> tone(double hz, double seconds, float amplitude = 0.5f)
{
    return ElementTestFixture::generateSine(hz, kRate, static_cast<int>(seconds * kRate), amplitude);
}

/// Harmonics of `f0` weighted by a bump of spectral energy centred on `formantHz`: a vowel
/// with one formant and nothing else to confuse where it is.
std::vector<float> vowel(double f0, double formantHz, double seconds)
{
    const int n = static_cast<int>(seconds * kRate);
    std::vector<float> out(static_cast<std::size_t>(n), 0.0f);
    for (int k = 1; k * f0 < 6000.0; ++k)
    {
        const double offset = (k * f0 - formantHz) / 250.0;
        const double amplitude = 0.08 * std::exp(-0.5 * offset * offset);
        for (int i = 0; i < n; ++i)
            out[static_cast<std::size_t>(i)] += static_cast<float>(amplitude * std::sin(2.0 * kPi * k * f0 * i / kRate));
    }
    return out;
}

struct Player
{
    ElementTestFixture rig{"TimeStretch", kRate};

    explicit Player(const std::string& path)
    {
        std::string error;
        const bool ok = rig.element->setOption("file", path, error);
        if (! ok) std::printf("  %s\n", error.c_str());
        assert(ok);
    }

    /// `seconds` of output, rendered in 512-sample blocks as a host would.
    std::vector<float> play(double seconds)
    {
        std::vector<float> out;
        for (int done = 0; done < static_cast<int>(seconds * kRate); done += 512)
        {
            const auto block = rig.run(std::vector<float>(512, 0.0f));
            out.insert(out.end(), block.begin(), block.end());
        }
        return out;
    }
};

float rms(const std::vector<float>& v, double fromSeconds, double toSeconds)
{
    const auto a = static_cast<std::size_t>(fromSeconds * kRate);
    const auto b = std::min(static_cast<std::size_t>(toSeconds * kRate), v.size());
    return ElementTestFixture::measureRms(std::vector<float>(v.begin() + static_cast<std::ptrdiff_t>(a),
                                                             v.begin() + static_cast<std::ptrdiff_t>(b)));
}

/// Frequency of the strongest partial in the last 16384 samples before `endSeconds`.
double pitchOf(const std::vector<float>& v, double endSeconds)
{
    const auto end = std::min(static_cast<std::size_t>(endSeconds * kRate), v.size());
    const std::vector<float> window(v.begin() + static_cast<std::ptrdiff_t>(end - 16384),
                                    v.begin() + static_cast<std::ptrdiff_t>(end));
    const auto bins = ElementTestFixture::computeSpectrum(window, 14);
    std::size_t peak = 4;
    for (std::size_t b = 5; b + 2 < bins.size(); ++b)
        if (bins[b] > bins[peak]) peak = b;

    const double a = std::log(std::max(bins[peak - 1], 1.0e-20f));
    const double b = std::log(std::max(bins[peak], 1.0e-20f));
    const double c = std::log(std::max(bins[peak + 1], 1.0e-20f));
    const double d = a - 2.0 * b + c;
    return (static_cast<double>(peak) + (d != 0.0 ? 0.5 * (a - c) / d : 0.0)) * kRate / 16384.0;
}

/// Where the smoothed spectrum peaks between 300 and 3000 Hz: the formant, once the
/// spacing of the harmonics has been blurred away by averaging over 400 Hz.
double envelopePeak(const std::vector<float>& v, double endSeconds)
{
    const auto end = std::min(static_cast<std::size_t>(endSeconds * kRate), v.size());
    const std::vector<float> window(v.begin() + static_cast<std::ptrdiff_t>(end - 16384),
                                    v.begin() + static_cast<std::ptrdiff_t>(end));
    const auto bins = ElementTestFixture::computeSpectrum(window, 14);
    const double hzPerBin = kRate / 16384.0;
    const int half = static_cast<int>(200.0 / hzPerBin);

    double best = 0.0, bestHz = 0.0;
    for (int centre = static_cast<int>(300.0 / hzPerBin); centre < static_cast<int>(3000.0 / hzPerBin); ++centre)
    {
        double sum = 0.0;
        for (int b = centre - half; b <= centre + half; ++b)
            sum += static_cast<double>(bins[static_cast<std::size_t>(b)]) * bins[static_cast<std::size_t>(b)];
        if (sum > best) { best = sum; bestHz = centre * hzPerBin; }
    }
    return bestHz;
}

void testPlainPlaybackIsTheFile()
{
    Player p(writeWav("tone", tone(1000.0, 4.0)));
    const auto out = p.play(2.0);
    const double hz = pitchOf(out, 2.0);
    const double level = rms(out, 0.5, 2.0) / (0.5 / std::sqrt(2.0));
    std::printf("  speed 1: %.1f Hz, level %.3f\n", hz, level);
    assert(std::abs(hz - 1000.0) < 2.0);
    assert(std::abs(level - 1.0) < 0.1);
}

void testSpeedChangesDurationNotPitch()
{
    const auto path = writeWav("tone2", tone(1000.0, 4.0));

    for (const float speed : { 0.5f, 2.0f })
    {
        Player p(path);
        p.rig.set("speed", speed);
        p.rig.set("loop", 0.0f);
        const auto out = p.play(9.0);

        const double hz = pitchOf(out, 1.5);
        const double length = 4.0 / speed;
        std::printf("  speed %.1f: %.1f Hz; sounding at %.1f s: %.3f, at %.1f s: %.5f\n", speed, hz,
                    length - 0.8, rms(out, length - 1.0, length - 0.6), length + 0.8, rms(out, length + 0.6, length + 1.0));
        assert(std::abs(hz - 1000.0) < 3.0);
        assert(rms(out, length - 1.0, length - 0.6) > 0.25f);   // still playing near the end
        assert(rms(out, length + 0.6, length + 1.0) < 0.005f);  // and over after it
    }
}

void testPitchChangesPitchNotDuration()
{
    const auto path = writeWav("tone3", tone(1000.0, 4.0));

    for (const float semitones : { 12.0f, 7.0f, -5.0f, -12.0f })
    {
        Player p(path);
        p.rig.set("pitch", semitones);
        p.rig.set("loop", 0.0f);
        const auto out = p.play(6.0);

        const double expected = 1000.0 * std::pow(2.0, semitones / 12.0);
        const double hz = pitchOf(out, 2.0);
        const double cents = 1200.0 * std::log2(hz / expected);
        std::printf("  pitch %+.0f: %.1f Hz (%.1f expected), %.1f cents; at 3.4 s %.3f, at 4.8 s %.5f\n",
                    semitones, hz, expected, cents, rms(out, 3.2, 3.6), rms(out, 4.6, 5.0));
        assert(std::abs(cents) < 15.0);
        assert(rms(out, 3.2, 3.6) > 0.25f);      // the length is the file's, not scaled by the shift
        assert(rms(out, 4.6, 5.0) < 0.005f);
    }
}

void testFreezeHoldsAMoment()
{
    Player p(writeWav("tone4", tone(1000.0, 4.0)));
    p.rig.set("speed", 0.0f);
    p.rig.set("start", 0.5f);
    const auto out = p.play(4.0);

    const float early = rms(out, 0.5, 1.5), late = rms(out, 2.5, 3.5);
    std::printf("  freeze: level %.3f then %.3f, %.1f Hz\n", early, late, pitchOf(out, 4.0));
    assert(early > 0.25f && std::abs(late - early) < 0.15f * early);
    assert(std::abs(pitchOf(out, 4.0) - 1000.0) < 3.0);
}

void testHeldFormantsStayPut()
{
    // A voice at 130 Hz with a formant at 700 Hz, moved up a fifth. Shifted plainly,
    // the resonance goes to about 1050 Hz with everything else. Held, it stays near 700.
    const auto path = writeWav("vowel", vowel(130.0, 700.0, 5.0));

    const auto formant = [&](float semitones, bool keep)
    {
        Player p(path);
        p.rig.set("pitch", semitones);
        p.rig.set("formant", keep ? 1.0f : 0.0f);
        return envelopePeak(p.play(4.0), 4.0);
    };

    const double source = formant(0.0f, false), shifted = formant(7.0f, false), kept = formant(7.0f, true);
    std::printf("  formant: source %.0f Hz, shifted +7 %.0f Hz, held %.0f Hz\n", source, shifted, kept);
    assert(std::abs(source - 700.0) < 150.0);
    assert(shifted > 1.3 * source);
    assert(std::abs(kept - source) < 0.25 * source);
}

void testNothingBlowsUp()
{
    std::vector<float> noise(static_cast<std::size_t>(3 * kRate));
    std::uint32_t seed = 12345u;
    for (auto& v : noise)
    {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        v = 0.5f * static_cast<float>(static_cast<std::int32_t>(seed)) * 4.6566129e-10f;
    }
    const auto path = writeWav("noise", noise);

    for (const auto& [speed, pitch] : { std::pair{4.0f, -24.0f}, {0.0f, 24.0f}, {4.0f, 24.0f}, {0.01f, 0.0f}, {1.0f, -24.0f} })
    {
        Player p(path);
        p.rig.set("speed", speed);
        p.rig.set("pitch", pitch);
        for (const float v : p.play(3.0))
            assert(std::isfinite(v) && std::abs(v) < 20.0f);
    }
}

void testTriggerRestartsAndStaysQuietUntilThen()
{
    Player p(writeWav("tone5", tone(1000.0, 4.0)));
    p.rig.set("trigger", 0.0f);
    assert(rms(p.play(0.5), 0.0, 0.5) == 0.0f);   // a connected trigger owns playback

    p.rig.set("trigger", 1.0f);
    const auto out = p.play(1.5);
    assert(rms(out, 0.5, 1.4) > 0.25f);
    assert(std::abs(pitchOf(out, 1.5) - 1000.0) < 3.0);
}

void testBadFilesAreRefused()
{
    ElementTestFixture rig("TimeStretch", kRate);
    std::string error;
    assert(! rig.element->setOption("file", "/no/such/file.wav", error));
    assert(! error.empty());

    // And with no file at all it is silent rather than undefined.
    const auto out = rig.run(std::vector<float>(2048, 0.0f));
    assert(ElementTestFixture::measureRms(out) == 0.0f);
}

}  // namespace

int main()
{
    testPlainPlaybackIsTheFile();
    testSpeedChangesDurationNotPitch();
    testPitchChangesPitchNotDuration();
    testFreezeHoldsAMoment();
    testHeldFormantsStayPut();
    testNothingBlowsUp();
    testTriggerRestartsAndStaysQuietUntilThen();
    testBadFilesAreRefused();

    std::printf("TimeStretchTest PASSED\n");
    return 0;
}

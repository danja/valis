// tests/shot/ShotReportTest.cpp
//
// The valis-shot measurements as the operation surface (and so MCP) calls them. Each test writes its own wav files and
// knows the answer. listen is checked only for what it must do without models: say why it cannot.

#include "valis/ShotReport.h"

#include <algorithm>
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

constexpr double kPi = 3.14159265358979323846;

/// A mono 16-bit PCM wav.
void writeWav(const std::string& path, const std::vector<float>& x, int rate)
{
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(static_cast<std::uint32_t>(36 + x.size() * 2)); f.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(1); u32(static_cast<std::uint32_t>(rate)); u32(static_cast<std::uint32_t>(rate * 2)); u16(2); u16(16);
    f.write("data", 4); u32(static_cast<std::uint32_t>(x.size() * 2));
    for (float v : x)
        u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f))));
}

std::vector<float> sine(double hz, double amplitude, int rate, double seconds)
{
    std::vector<float> x(static_cast<std::size_t>(rate * seconds));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
    return x;
}

/// The reports are compact; spaces are dropped so a test can look for "key":value regardless.
std::string squeezed(std::string text)
{
    text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
    return text;
}

double number(const std::string& report, const std::string& key)
{
    const auto json = squeezed(report);
    const auto at = json.find("\"" + key + "\":");
    assert(at != std::string::npos && "key missing from the report");
    return std::stod(json.substr(at + key.size() + 3));
}

}  // namespace

int main()
{
    const auto dir = std::filesystem::temp_directory_path() / "valis-shot-report-test";
    std::filesystem::create_directories(dir);
    const auto quiet = (dir / "quiet.wav").string(), loud = (dir / "loud.wav").string(), other = (dir / "other.wav").string();
    writeWav(quiet, sine(440.0, 0.1, 48000, 3.0), 48000);      // 0.1 peak: -20 dBFS
    writeWav(loud, sine(440.0, 0.2, 48000, 3.0), 48000);       // 6 dB louder
    writeWav(other, sine(440.0, 0.1, 44100, 3.0), 44100);

    // measure: the pitch, the loudness and the envelope all come back, and the loudness is the known one.
    const auto m = shotMeasure(quiet);
    assert(m.ok);
    assert(squeezed(m.json).find("\"note\":\"A4\"") != std::string::npos);
    assert(squeezed(m.json).find("\"loudness\":") != std::string::npos && squeezed(m.json).find("\"envelope\":") != std::string::npos);
    const double lufs = number(m.json, "integratedLufs");
    std::printf("  0.1 sine: %.2f LUFS\n", lufs);
    assert(lufs < -15.0 && lufs > -30.0);      // printed above; checked against the Loudness test, not re-derived here
    assert(number(m.json, "truePeakDbtp") < -19.0 && number(m.json, "truePeakDbtp") > -21.0);

    // A quote in a file name must not break the JSON (the first version printed names unescaped).
    const auto quoted = (dir / "a\"b.wav").string();
    writeWav(quoted, sine(440.0, 0.1, 48000, 1.0), 48000);
    const auto q = shotMeasure(quoted);
    assert(q.ok && squeezed(q.json).find("a\\\"b.wav") != std::string::npos);

    // compare: the same file is no distance, 6 dB louder reads about 6 dB and 6 LU, and a different rate is refused.
    const auto same = shotCompare(quiet, quiet);
    assert(same.ok && number(same.json, "spectralDistanceDb") < 0.01);
    const auto louder = shotCompare(quiet, loud);
    assert(louder.ok);
    std::printf("  6 dB louder: %.2f dB apart, %.2f LU\n", number(louder.json, "spectralDistanceDb"),
                number(louder.json, "loudnessDifferenceLu"));
    assert(std::abs(number(louder.json, "spectralDistanceDb") - 6.0) < 1.0);
    assert(std::abs(number(louder.json, "loudnessDifferenceLu") - 6.0) < 0.3);
    const auto mismatch = shotCompare(quiet, other);
    assert(! mismatch.ok && mismatch.error.find("sample rates differ") != std::string::npos);

    // compare on a long file says which section changed (and says nothing about sections for a short one).
    assert(squeezed(louder.json).find("\"sections\"") == std::string::npos);
    {
        std::vector<float> original, edited;
        for (int part = 0; part < 3; ++part)
        {
            const auto tone = sine(part == 1 ? 392.0 : 261.6, part == 1 ? 0.4 : 0.2, 24000, 20.0);
            original.insert(original.end(), tone.begin(), tone.end());
            auto changed = tone;
            if (part == 1)
                for (auto& s : changed) s *= 0.5f;
            edited.insert(edited.end(), changed.begin(), changed.end());
        }
        const auto before = (dir / "song-before.wav").string(), after = (dir / "song-after.wav").string();
        writeWav(before, original, 24000);
        writeWav(after, edited, 24000);
        const auto where = shotCompare(before, after);
        assert(where.ok);
        assert(squeezed(where.json).find("\"sections\":[{\"startS\":0,") != std::string::npos);
        assert(number(where.json, "mostChangedSection") == 1.0);
    }

    assert(! shotIs(quiet, "").ok);

    // Failures are located and say what to do, never a crash.
    assert(! shotMeasure("").ok);
    assert(! shotMeasure((dir / "missing.wav").string()).ok);
    assert(! shotCompare(quiet, (dir / "missing.wav").string()).ok);
    std::ofstream((dir / "notaudio.wav").string()) << "not audio at all";
    assert(! shotMeasure((dir / "notaudio.wav").string()).ok);

    // listen: with the models absent or not built in, it fails and says why; with them it answers.
    std::string reason;
    const bool canListen = shotCanListen(reason);
    const auto heard = shotListen(quiet, {"a pure sine tone", "a violin"});
    if (canListen)
    {
        assert(heard.ok && heard.json.find("\"matches\":") != std::string::npos);
        std::printf("  listen: %s\n", heard.json.c_str());

        // is: a pure tone is a pure sine tone, and is not a human voice singing.
        const auto yes = shotIs(quiet, "a pure sine tone"), no = shotIs(quiet, "a human voice singing");
        assert(yes.ok && no.ok);
        std::printf("  is: %s\n  is: %s\n", yes.json.c_str(), no.json.c_str());
        assert(squeezed(yes.json).find("\"verdict\":\"yes\"") != std::string::npos);
        assert(squeezed(no.json).find("\"verdict\":\"no\"") != std::string::npos);
        assert(squeezed(yes.json).find("\"rank\":1,") != std::string::npos);
    }
    else
    {
        assert(! heard.ok && ! heard.error.empty());
        assert(! shotIs(quiet, "a trumpet").ok);
        std::printf("  listen unavailable: %s\n", heard.error.c_str());
    }

    std::filesystem::remove_all(dir);
    std::printf("ShotReportTest PASSED\n");
    return 0;
}

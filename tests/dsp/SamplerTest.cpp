// tests/dsp/SamplerTest.cpp
//
// val:Sampler. A sampler is held to what it plays: a note must sound at the pitch its key asks for, at any host
// rate; a table must send each key and velocity to its own sound; a hit must stop where a drum hit stops and a
// held note where a held note does; and a bad table must fail the load with the line that was wrong.
//
// The sounds are sine waves of known frequency written to disk here, so the pitch of what comes out can be read
// exactly.

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

constexpr double kFileRate = 48000.0;

std::string directory()
{
    static const auto path = (std::filesystem::temp_directory_path() / "valis-sampler-test").string();
    std::filesystem::create_directories(path);
    return path;
}

/// A mono 16-bit WAV of a sine at `hz`, `seconds` long, with a short fade in and out so it does not click.
std::string sineFile(const std::string& name, double hz, double seconds)
{
    const auto path = directory() + "/" + name;
    const auto frames = static_cast<std::uint32_t>(seconds * kFileRate);

    std::ofstream out(path, std::ios::binary);
    const auto put32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4); put32(36 + frames * 2); out.write("WAVEfmt ", 8);
    put32(16); put16(1); put16(1); put32(static_cast<std::uint32_t>(kFileRate)); put32(static_cast<std::uint32_t>(kFileRate) * 2);
    put16(2); put16(16); out.write("data", 4); put32(frames * 2);
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        const double fade = std::min({1.0, i / 200.0, (frames - i) / 200.0});
        const double v = 0.5 * fade * std::sin(2.0 * M_PI * hz * i / kFileRate);
        put16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v * 32767.0))));
    }
    return path;
}

struct Rig
{
    ElementTestFixture fixture;

    explicit Rig(double rate = 48000.0) : fixture("Sampler", rate) {}

    void option(const std::string& key, const std::string& value)
    {
        std::string error;
        const bool ok = fixture.element->setOption(key, value, error);
        if (! ok) std::printf("setOption %s failed: %s\n", key.c_str(), error.c_str());
        assert(ok);
    }

    std::vector<float> play(double seconds, double rate = 48000.0)
    {
        return fixture.run(std::vector<float>(static_cast<std::size_t>(seconds * rate), 0.0f));
    }
};

/// Frequency by counting rising zero crossings over the loudest stretch of `signal`.
double frequencyOf(const std::vector<float>& signal, double rate, std::size_t from, std::size_t to)
{
    int crossings = 0;
    std::size_t first = 0, last = 0;
    for (std::size_t i = std::max<std::size_t>(from, 1); i < std::min(to, signal.size()); ++i)
        if (signal[i - 1] < 0.0f && signal[i] >= 0.0f)
        {
            if (crossings++ == 0) first = i;
            last = i;
        }
    return crossings > 1 ? (crossings - 1) * rate / static_cast<double>(last - first) : 0.0;
}

float rms(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < std::min(to, x.size()); ++i) sum += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(sum / static_cast<double>(std::max<std::size_t>(to - from, 1))));
}

double cents(double measured, double wanted) { return 1200.0 * std::log2(measured / wanted); }

void testPitchFollowsTheKeyAtAnyRate()
{
    const auto file = sineFile("a440.wav", 440.0, 1.0);
    for (const double rate : {44100.0, 48000.0, 96000.0})
        for (const int note : {57, 69, 81, 76})
        {
            Rig rig(rate);
            rig.option("file", file);
            rig.option("rootNote", "69");
            rig.fixture.setNote(note, 0.8f, true);
            const auto out = rig.play(0.5, rate);
            const double wanted = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            const double got = frequencyOf(out, rate, static_cast<std::size_t>(0.1 * rate), static_cast<std::size_t>(0.45 * rate));
            std::printf("rate %.0f note %d: %.2f Hz, wanted %.2f (%.1f cents)\n", rate, note, got, wanted, cents(got, wanted));
            assert(std::abs(cents(got, wanted)) < 5.0);
        }
}

void testHeldNotesLoopAndReleaseEnds()
{
    Rig rig;
    rig.option("file", sineFile("short.wav", 440.0, 0.25));
    rig.option("rootNote", "69");
    rig.fixture.setNote(69, 1.0f, true);
    const auto held = rig.play(1.0);
    assert(rms(held, 40000, 48000) > 0.1f);          // a quarter-second sound is still sounding after a second

    rig.fixture.setNote(69, 1.0f, false);
    const auto after = rig.play(0.5);
    assert(rms(after, 400, 900) > 0.05f);             // it fades, it does not stop dead
    assert(rms(after, 4000, 8000) < 1.0e-4f);         // and is gone after the 30 ms release
}

void testLoopOffPlaysOnceThroughAndStops()
{
    Rig rig;
    rig.option("file", sineFile("short2.wav", 440.0, 0.25));
    rig.fixture.set("loop", 0.0f);
    rig.fixture.setNote(60, 1.0f, true);
    const auto out = rig.play(1.0);
    assert(rms(out, 2000, 9000) > 0.1f);
    assert(rms(out, 20000, 48000) < 1.0e-4f);
}

void testOneShotIgnoresNoteOff()
{
    Rig rig;
    rig.option("file", sineFile("hit.wav", 300.0, 0.5));
    rig.fixture.set("oneshot", 1.0f);
    rig.fixture.set("keytrack", 0.0f);
    rig.fixture.setNote(40, 1.0f, true);
    rig.play(0.01);
    rig.fixture.setNote(40, 1.0f, false);
    const auto out = rig.play(0.6);
    assert(rms(out, 4800, 14400) > 0.1f);             // keeps playing after the key is up
    assert(rms(out, 26000, 28800) < 1.0e-3f);         // and ends with the sound
    assert(std::abs(cents(frequencyOf(out, 48000.0, 4800, 14400), 300.0)) < 5.0);   // at its own pitch, not the key's
}

void testATableSendsEachKeyToItsSound()
{
    const auto low  = sineFile("low.wav", 200.0, 1.0);
    const auto high = sineFile("high.wav", 800.0, 1.0);
    Rig rig;
    rig.option("samples", "file=" + low + " root=60 keys=0-65\nfile=" + high + " root=72 keys=66-127");

    const struct { int note; double wanted; } cases[] = {{60, 200.0}, {48, 100.0}, {72, 800.0}, {79, 800.0 * std::pow(2.0, 7 / 12.0)}};
    for (const auto& c : cases)
    {
        Rig r;
        r.option("samples", "file=" + low + " root=60 keys=0-65\nfile=" + high + " root=72 keys=66-127");
        r.fixture.setNote(c.note, 0.8f, true);
        const auto out = r.play(0.4);
        const double got = frequencyOf(out, 48000.0, 4800, 19200);
        std::printf("key %d: %.1f Hz, wanted %.1f\n", c.note, got, c.wanted);
        assert(std::abs(cents(got, c.wanted)) < 5.0);
    }
}

void testVelocityLayersAndRoundRobin()
{
    const auto soft = sineFile("soft.wav", 200.0, 1.0);
    const auto hard = sineFile("hard.wav", 600.0, 1.0);
    const std::string layers = "file=" + soft + " key=40 vel=1-64\nfile=" + hard + " key=40 vel=65-127";
    for (const auto& [velocity, wanted] : {std::pair{0.3f, 200.0}, std::pair{0.9f, 600.0}})
    {
        Rig rig;
        rig.option("samples", layers);
        rig.fixture.setNote(40, velocity, true);
        const auto out = rig.play(0.4);
        assert(std::abs(cents(frequencyOf(out, 48000.0, 4800, 19200), wanted)) < 5.0);
    }

    // Two sounds on one key and velocity take turns.
    Rig rig;
    rig.option("samples", "file=" + soft + " key=40\nfile=" + hard + " key=40");
    rig.fixture.set("oneshot", 1.0f);
    double heard[4];
    for (int hit = 0; hit < 4; ++hit)
    {
        rig.fixture.setNote(40, 0.8f, true);
        const auto out = rig.play(0.2);
        heard[hit] = frequencyOf(out, 48000.0, 2400, 9000);
        rig.fixture.setNote(40, 0.8f, false);
        rig.play(0.05);
    }
    assert(std::abs(heard[0] - heard[2]) < 5.0 && std::abs(heard[1] - heard[3]) < 5.0);
    assert(std::abs(heard[0] - heard[1]) > 100.0);
}

void testAKeyNoZoneAnswersPlaysTheNearestOne()
{
    Rig rig;
    rig.option("samples", "file=" + sineFile("only.wav", 440.0, 1.0) + " root=60 keys=50-70");
    rig.fixture.setNote(100, 0.8f, true);
    const auto out = rig.play(0.3);
    assert(rms(out, 4800, 14400) > 0.05f);
}

void testAHitCutsOthersInItsChokeGroup()
{
    const auto open   = sineFile("open.wav", 300.0, 1.0);
    const auto closed = sineFile("closed.wav", 900.0, 1.0);
    Rig rig;
    rig.option("samples", "file=" + open + " key=46 choke=1\nfile=" + closed + " key=42 choke=1");
    rig.fixture.set("oneshot", 1.0f);
    rig.fixture.setNote(46, 1.0f, true);
    const auto first = rig.play(0.1);
    assert(rms(first, 2400, 4800) > 0.1f);

    rig.fixture.setNote(42, 1.0f, true);
    const auto second = rig.play(0.3);
    // The open sound is cut within a few milliseconds; what is left is the closed one.
    const double got = frequencyOf(second, 48000.0, 2400, 12000);
    assert(std::abs(cents(got, 900.0)) < 5.0);
}

void testALoopWithPointsHoldsThenReleases()
{
    Rig rig;
    rig.option("samples", "file=" + sineFile("looped.wav", 440.0, 1.0) + " root=69 key=69 loopStart=0.2 loopEnd=0.4 fade=0.01");
    rig.fixture.setNote(69, 1.0f, true);
    const auto held = rig.play(2.0);
    assert(rms(held, 90000, 96000) > 0.1f);            // sustains well past the sound's one second
    assert(std::abs(cents(frequencyOf(held, 48000.0, 48000, 90000), 440.0)) < 10.0);
    rig.fixture.setNote(69, 1.0f, false);
    const auto after = rig.play(0.3);
    assert(rms(after, 6000, 9000) < 1.0e-4f);
}

void testABadTableFailsTheLoadWithTheLine()
{
    Rig rig;
    std::string error;
    assert(! rig.fixture.element->setOption("samples", "# kit\nfile=" + sineFile("ok.wav", 100.0, 0.1) + " key=36\nfile=/no/such/file.wav key=38", error));
    assert(error.find("line 3") != std::string::npos && error.find("no such file") != std::string::npos);

    assert(! rig.fixture.element->setOption("samples", "file=" + sineFile("ok.wav", 100.0, 0.1) + " colour=red", error));
    assert(error.find("unknown word 'colour'") != std::string::npos);

    assert(! rig.fixture.element->setOption("samples", "key=36", error));
    assert(error.find("needs file=") != std::string::npos);

    assert(! rig.fixture.element->setOption("samples", "file=" + sineFile("ok.wav", 100.0, 0.1) + " keys=70-60", error));
    assert(! rig.fixture.element->setOption("samples", "file=" + sineFile("ok.wav", 100.0, 0.1) + " sha256=" + std::string(64, '0'), error));
    assert(error.find("sha256") != std::string::npos);

    // A failed table leaves what was there playing.
    rig.option("file", sineFile("kept.wav", 440.0, 0.5));
    assert(! rig.fixture.element->setOption("samples", "key=36", error));
    rig.fixture.setNote(60, 1.0f, true);
    assert(rms(rig.play(0.2), 2400, 9000) > 0.05f);
}

void testNothingLoadedIsSilent()
{
    Rig rig;
    rig.fixture.setNote(60, 1.0f, true);
    assert(rms(rig.play(0.1), 0, 4800) == 0.0f);
}

void testManyNotesAtOnceStayBounded()
{
    Rig rig;
    rig.option("file", sineFile("pad.wav", 220.0, 2.0));
    rig.fixture.set("oneshot", 1.0f);
    for (int note = 30; note < 80; ++note)
    {
        rig.fixture.setNote(note, 0.9f, true);
        const auto out = rig.play(0.01);
        for (const float v : out) assert(std::isfinite(v) && std::abs(v) < 20.0f);
    }
}

}  // namespace

int main()
{
    testPitchFollowsTheKeyAtAnyRate();
    testHeldNotesLoopAndReleaseEnds();
    testLoopOffPlaysOnceThroughAndStops();
    testOneShotIgnoresNoteOff();
    testATableSendsEachKeyToItsSound();
    testVelocityLayersAndRoundRobin();
    testAKeyNoZoneAnswersPlaysTheNearestOne();
    testAHitCutsOthersInItsChokeGroup();
    testALoopWithPointsHoldsThenReleases();
    testABadTableFailsTheLoadWithTheLine();
    testNothingLoadedIsSilent();
    testManyNotesAtOnceStayBounded();
    std::puts("SamplerTest passed");
    return 0;
}

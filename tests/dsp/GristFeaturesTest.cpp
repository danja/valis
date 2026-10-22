// tests/dsp/GristFeaturesTest.cpp
//
// What the granulator and its neighbours took from the Grist plugin: grain
// pitches drawn from a scale, grain length variance, a percussive window, a
// seed, monitoring outputs, a guard against grains running into the write head,
// val:Quantise, and val:Transport's period.

#include "dsp/GrainPool.h"
#include "dsp/Scale.h"
#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

double amplitudeAt(const std::vector<float>& s, double hz)
{
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        const double phase = 2.0 * kPi * hz * static_cast<double>(i) / kRate;
        re += s[i] * std::cos(phase);
        im += s[i] * std::sin(phase);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(s.size());
}

// ---------------------------------------------------------------- scales

void testScaleMembership()
{
    using dsp::scaleAllows;

    assert(scaleAllows(1, 5) && scaleAllows(1, -7));              // semitones: anything
    assert(scaleAllows(2, 12) && scaleAllows(2, -24) && ! scaleAllows(2, 7));
    assert(scaleAllows(3, 7) && scaleAllows(3, -5) && ! scaleAllows(3, 4));
    assert(scaleAllows(4, 4) && ! scaleAllows(4, 6) && scaleAllows(4, -1));
    assert(scaleAllows(5, 3) && ! scaleAllows(5, 4));
    assert(scaleAllows(6, 9) && ! scaleAllows(6, 5));
}

void testNearestInScale()
{
    using dsp::nearestInScale;

    assert(nearestInScale(4, 4.0f) == 4.0f);                      // already on it
    assert(nearestInScale(4, 1.0f) == 0.0f);                      // halfway: the lower
    assert(nearestInScale(4, 6.0f) == 5.0f);
    assert(nearestInScale(4, 1.4f) == 2.0f);                      // nearer 2 than 0? 1.4 is nearer 2
    assert(nearestInScale(4, -1.0f) == -1.0f);                    // B below the root
    assert(nearestInScale(2, 7.0f) == 12.0f);                     // octaves only
    assert(nearestInScale(2, 5.0f) == 0.0f);
    assert(std::abs(nearestInScale(0, 4.3f) - 4.3f) < 1.0e-6f);   // free is untouched

    // A root moves the scale: D major has F sharp (6) and no F (5).
    assert(nearestInScale(4, 6.0f, 2) == 6.0f);
    assert(nearestInScale(4, 5.0f, 2) == 4.0f);
}

void testDrawsStayOnTheScale()
{
    dsp::Random random(1234u);
    std::set<int> seen;

    for (int i = 0; i < 4000; ++i)
    {
        const float offset = dsp::drawOffset(random, 0, 12.0f, dsp::Direction::Both,
                                             dsp::ScaleKind::Major);
        assert(offset == std::round(offset));
        assert(std::abs(offset) <= 12.0f);
        assert(dsp::scaleAllows(4, static_cast<int>(offset)));
        seen.insert(static_cast<int>(offset));
    }
    assert(seen.size() > 10);                                     // it does explore

    for (int i = 0; i < 500; ++i)
    {
        assert(dsp::drawOffset(random, 0, 12.0f, dsp::Direction::Up, dsp::ScaleKind::Semitones) >= 0.0f);
        assert(dsp::drawOffset(random, 0, 12.0f, dsp::Direction::Down, dsp::ScaleKind::Semitones) <= 0.0f);
        const float free = dsp::drawOffset(random, 0, 5.0f, dsp::Direction::Both, dsp::ScaleKind::Free);
        assert(free >= -5.0f && free <= 5.0f);
    }

    assert(dsp::drawOffset(random, 0, 0.0f, dsp::Direction::Both, dsp::ScaleKind::Major) == 0.0f);
}

void testQuantiseElement()
{
    ElementTestFixture rig("Quantise");
    rig.set("scale", 4.0f);
    rig.set("in", 6.0f);
    rig.run({0.0f});
    assert(rig.lastControlOut[0] == 5.0f);

    rig.set("root", 2.0f);
    rig.run({0.0f});
    assert(rig.lastControlOut[0] == 6.0f);

    rig.set("scale", 0.0f);
    rig.set("in", 6.3f);
    rig.run({0.0f});
    assert(std::abs(rig.lastControlOut[0] - 6.3f) < 1.0e-6f);
}

// ---------------------------------------------------------------- window

void testPercussiveWindow()
{
    const dsp::PercussiveWindow window;

    double mean = 0.0;
    for (int i = 0; i < 1000; ++i)
        mean += window.at(static_cast<float>(i) / 1000.0f);
    mean /= 1000.0;
    assert(std::abs(mean - 0.5) < 0.01);                          // the level of a raised cosine

    assert(window.at(0.0f) < 0.01f);                              // starts from nothing
    assert(window.at(0.08f) > 3.0f * window.at(0.5f));            // peaks early
    assert(window.at(0.99f) < window.at(0.5f));                   // and falls
}

// ---------------------------------------------------------------- granulator

constexpr int kBuffer = 8192;

double exactFrequency(int periods) { return kRate * periods / kBuffer; }

/// A frozen buffer of a sine with a whole number of periods, ready to play.
void fill(ElementTestFixture& rig, int periods)
{
    rig.set("freeze", 0.0f);
    rig.set("density", 0.01f);
    rig.run(ElementTestFixture::generateSine(exactFrequency(periods), kRate, kBuffer));
    rig.set("freeze", 1.0f);
}

std::vector<float> play(ElementTestFixture& rig, int n = 24000)
{
    return rig.run(std::vector<float>(static_cast<std::size_t>(n), 0.0f), "left");
}

void configure(ElementTestFixture& rig)
{
    rig.set("density", 60.0f);
    rig.set("size", 60.0f);
    rig.set("spread", 0.0f);
    rig.set("spray", 0.0f);
    rig.set("position", 0.5f);
}

void testRangeDrawsPitchesFromTheScale()
{
    constexpr int periods = 84;
    const double f = exactFrequency(periods);

    // Octaves, up, range 12: each grain is either on the pitch or an octave up.
    ElementTestFixture octave("Granulator", kRate);
    fill(octave, periods);
    configure(octave);
    octave.set("range", 12.0f);
    octave.set("direction", 0.0f);
    octave.set("scale", 2.0f);
    const auto shifted = play(octave);

    ElementTestFixture plain("Granulator", kRate);
    fill(plain, periods);
    configure(plain);
    const auto unshifted = play(plain);

    assert(amplitudeAt(unshifted, 2.0 * f) < 0.02 * amplitudeAt(unshifted, f));
    assert(amplitudeAt(shifted, 2.0 * f) > 0.1 * amplitudeAt(shifted, f));
    assert(amplitudeAt(shifted, f) > 0.1 * amplitudeAt(unshifted, f));   // and still on the pitch
}

void testRangeOffLeavesTheRenderAlone()
{
    // With nothing random in the grain's placement, a scale that only allows
    // the pitch itself must render exactly as no range does. It also proves the
    // new draws are taken only when asked for.
    ElementTestFixture off("Granulator", kRate), narrow("Granulator", kRate);
    for (auto* rig : {&off, &narrow})
    {
        fill(*rig, 84);
        configure(*rig);
        rig->set("reverse", 0.0f);
    }
    narrow.set("range", 7.0f);
    narrow.set("scale", 2.0f);                    // octaves: nothing within seven semitones

    assert(play(off) == play(narrow));
}

void testSizeJitterShortensGrains()
{
    ElementTestFixture steady("Granulator", kRate), ragged("Granulator", kRate);
    for (auto* rig : {&steady, &ragged})
    {
        fill(*rig, 84);
        configure(*rig);
        rig->set("shape", 1.0f);
    }
    ragged.set("sizeJitter", 1.0f);

    const auto a = play(steady), b = play(ragged);
    assert(a != b);
    assert(ElementTestFixture::measureRms(b) < ElementTestFixture::measureRms(a));
}

void testCurveKeepsTheLevelClose()
{
    ElementTestFixture hann("Granulator", kRate), struck("Granulator", kRate);
    for (auto* rig : {&hann, &struck})
    {
        fill(*rig, 84);
        configure(*rig);
        rig->set("shape", 1.0f);                  // a full raised cosine, average 0.5
    }
    struck.set("curve", 1.0f);

    const auto a = play(hann), b = play(struck);
    assert(a != b);
    const float ratio = ElementTestFixture::measureRms(b) / ElementTestFixture::measureRms(a);
    // The same average height, but a peakier shape carries more energy for it.
    assert(ratio > 0.9f && ratio < 1.6f);
}

void testSeedChoosesTheCloud()
{
    const auto render = [](float seed)
    {
        ElementTestFixture rig("Granulator", kRate);
        fill(rig, 84);
        configure(rig);
        rig.set("range", 12.0f);
        rig.set("scale", 1.0f);
        rig.set("seed", seed);
        return play(rig);
    };

    assert(render(7.0f) == render(7.0f));
    assert(render(7.0f) != render(8.0f));
}

void testOutputsReportGrains()
{
    ElementTestFixture rig("Granulator", kRate);
    fill(rig, 84);
    configure(rig);
    rig.set("density", 100.0f);

    int onsets = 0;
    float most = 0.0f;
    for (int slice = 0; slice < 1500; ++slice)
    {
        rig.run(std::vector<float>(32, 0.0f), "left");
        most = std::max(most, rig.lastControlOut[0]);
        onsets += rig.lastControlOut[1] > 0.5f ? 1 : 0;
    }

    assert(most >= 3.0f);                         // 60 ms grains at 100 a second overlap
    assert(onsets > 50 && onsets < 1500);         // pulses, not a constant
}

/// Recording live, a grain that starts at the write head and plays faster than
/// real time would run into the oldest audio in the buffer. Here the oldest
/// audio is a low tone and the newest a high one, so the low tone showing up in
/// the output means a grain crossed the head.
void testGrainsStayBehindTheWriteHead()
{
    ElementTestFixture rig("Granulator", kRate);
    rig.set("freeze", 0.0f);
    rig.set("density", 0.01f);
    rig.set("position", 1.0f);
    rig.set("size", 40.0f);
    rig.set("spread", 0.0f);

    constexpr int half = 24000;
    const double low = 500.0, high = 2000.0;
    rig.run(ElementTestFixture::generateSine(low, kRate, half));

    rig.set("density", 80.0f);
    rig.set("pitch", 12.0f);
    const auto out = rig.run(ElementTestFixture::generateSine(high, kRate, half), "left");

    // A grain starting at the head would read the low tone, an octave up.
    const double fromOldest = amplitudeAt(out, 2.0 * low);
    const double fromNewest = amplitudeAt(out, 2.0 * high);
    assert(fromNewest > 0.01);
    assert(fromOldest < 0.1 * fromNewest);
}

// ---------------------------------------------------------------- transport

void testTransportPeriod()
{
    ElementTestFixture rig("Transport", kRate);
    rig.setTransport(true, 120.0, 0.0);
    rig.set("division", 1.0f);
    rig.run(std::vector<float>(32, 0.0f));
    assert(std::abs(rig.lastControlOut[5] - 500.0f) < 0.01f);     // a beat at 120 bpm

    rig.set("division", 0.25f);
    rig.run(std::vector<float>(32, 0.0f));
    assert(std::abs(rig.lastControlOut[5] - 125.0f) < 0.01f);

    rig.setTransport(true, 90.0, 0.0);
    rig.run(std::vector<float>(32, 0.0f));
    assert(std::abs(rig.lastControlOut[5] - 1000.0f * 60.0f / 90.0f * 0.25f) < 0.01f);
}

}  // namespace

int main()
{
    testScaleMembership();
    testNearestInScale();
    testDrawsStayOnTheScale();
    testQuantiseElement();
    testPercussiveWindow();
    testRangeDrawsPitchesFromTheScale();
    testRangeOffLeavesTheRenderAlone();
    testSizeJitterShortensGrains();
    testCurveKeepsTheLevelClose();
    testSeedChoosesTheCloud();
    testOutputsReportGrains();
    testGrainsStayBehindTheWriteHead();
    testTransportPeriod();

    std::puts("GristFeaturesTest passed");
    return 0;
}

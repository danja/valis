// tests/dsp/FMTest.cpp
//
// val:FMOperator and val:RateLevelEnvelope. The operator is checked against the
// closed form for phase modulation, where the sideband amplitudes are Bessel
// functions of the index, so the test knows what the spectrum must be rather
// than only that it changed.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

/// Amplitude of one frequency by direct correlation. The signals here are a
/// whole number of cycles of it, so no window is needed.
double amplitudeAt(const std::vector<float>& signal, double hz)
{
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < signal.size(); ++i)
    {
        const double phase = 2.0 * kPi * hz * static_cast<double>(i) / kRate;
        re += signal[i] * std::cos(phase);
        im += signal[i] * std::sin(phase);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(signal.size());
}

float peak(const std::vector<float>& signal)
{
    float p = 0.0f;
    for (const float v : signal)
        p = std::max(p, std::abs(v));
    return p;
}

constexpr int kCycles = 480;
constexpr double kHz = 480.0;                  // 100 samples a cycle
constexpr int kSamples = kCycles * 100;

std::vector<float> sine()
{
    return ElementTestFixture::generateSine(kHz, kRate, kSamples);
}

void testPlainSine()
{
    ElementTestFixture rig("FMOperator");
    rig.set("frequency", static_cast<float>(kHz));
    const auto out = rig.run(std::vector<float>(kSamples, 0.0f));

    assert(std::abs(amplitudeAt(out, kHz) - 1.0) < 0.01);
    assert(amplitudeAt(out, 2 * kHz) < 0.01);

    // Ratio multiplies the pitch, and fixed replaces it.
    ElementTestFixture octave("FMOperator");
    octave.set("frequency", static_cast<float>(kHz));
    octave.set("ratio", 2.0f);
    assert(std::abs(amplitudeAt(octave.run(std::vector<float>(kSamples, 0.0f)), 2 * kHz) - 1.0) < 0.01);

    ElementTestFixture fixedRig("FMOperator");
    fixedRig.set("frequency", 100.0f);
    fixedRig.set("fixed", static_cast<float>(kHz));
    assert(std::abs(amplitudeAt(fixedRig.run(std::vector<float>(kSamples, 0.0f)), kHz) - 1.0) < 0.01);
}

void testLevelIsThreeQuartersOfADecibelAStep()
{
    ElementTestFixture rig("FMOperator");
    rig.set("frequency", static_cast<float>(kHz));
    rig.set("level", 89.0f);                         // ten steps down
    const double ratio = peak(rig.run(std::vector<float>(kSamples, 0.0f)));
    assert(std::abs(ratio - std::pow(2.0, -10.0 / 8.0)) < 0.01);

    ElementTestFixture silent("FMOperator");
    silent.set("level", 0.0f);
    assert(peak(silent.run(std::vector<float>(kSamples, 0.0f))) == 0.0f);

    ElementTestFixture quiet("FMOperator");
    quiet.set("amp", 0.0f);
    assert(peak(quiet.run(std::vector<float>(kSamples, 0.0f))) == 0.0f);
}

/// A carrier at 480 Hz modulated by a sine at 120 Hz, index 2. The spectrum is
/// the carrier and sidebands every 120 Hz, whose amplitudes are J_n(2).
void testModulationSidebands()
{
    constexpr double kModulator = 120.0;
    const auto modulator = [](double amplitude)
    {
        return ElementTestFixture::generateSine(kModulator, kRate, kSamples,
                                                static_cast<float>(amplitude));
    };

    const double j0 = 0.2238908, j1 = 0.5767248, j2 = 0.3528340, j3 = 0.1289432;

    ElementTestFixture rig("FMOperator");
    rig.set("frequency", static_cast<float>(kHz));
    rig.set("depth", 2.0f);
    const auto out = rig.run(modulator(1.0));

    assert(std::abs(amplitudeAt(out, kHz) - j0) < 0.01);
    assert(std::abs(amplitudeAt(out, kHz + kModulator) - j1) < 0.01);
    assert(std::abs(amplitudeAt(out, kHz - kModulator) - j1) < 0.01);
    assert(std::abs(amplitudeAt(out, kHz + 2 * kModulator) - j2) < 0.01);
    assert(std::abs(amplitudeAt(out, kHz + 3 * kModulator) - j3) < 0.01);

    // Index is depth times the modulator's amplitude.
    ElementTestFixture half("FMOperator");
    half.set("frequency", static_cast<float>(kHz));
    half.set("depth", 4.0f);
    assert(std::abs(amplitudeAt(half.run(modulator(0.5)), kHz + 2 * kModulator) - j2) < 0.01);

    // With nothing on pm it is the plain sine again.
    ElementTestFixture bare("FMOperator");
    bare.set("frequency", static_cast<float>(kHz));
    bare.set("depth", 2.0f);
    assert(amplitudeAt(bare.run(std::vector<float>(kSamples, 0.0f)), kHz + kModulator) < 0.01);
}

void testFeedbackAddsHarmonics()
{
    ElementTestFixture plain("FMOperator");
    plain.set("frequency", static_cast<float>(kHz));
    const auto flat = plain.run(std::vector<float>(kSamples, 0.0f));

    ElementTestFixture fb("FMOperator");
    fb.set("frequency", static_cast<float>(kHz));
    fb.set("feedback", 7.0f);
    const auto bright = fb.run(std::vector<float>(kSamples, 0.0f));

    assert(amplitudeAt(flat, 2 * kHz) < 0.01);
    assert(amplitudeAt(bright, 2 * kHz) > 0.1);
    assert(amplitudeAt(bright, 3 * kHz) > 0.05);
    for (const float v : bright)
        assert(std::isfinite(v) && std::abs(v) <= 1.5f);
}

void testOperatorIsDeterministic()
{
    ElementTestFixture a("FMOperator"), b("FMOperator");
    for (auto* rig : {&a, &b})
    {
        rig->set("frequency", 330.0f);
        rig->set("feedback", 4.0f);
    }
    assert(a.run(sine()) == b.run(sine()));
}

float largestDifference(const std::vector<float>& a, const std::vector<float>& b)
{
    assert(a.size() == b.size());
    float d = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        d = std::max(d, std::abs(a[i] - b[i]));
    return d;
}

const std::vector<float> kSilence(kSamples, 0.0f);

/// With no loop the matrix must be the same instrument as the separate
/// operators: a modulator numbered higher than its target is used on the same
/// sample, exactly as a connected element's output would be.
void testMatrixMatchesSeparateOperators()
{
    ElementTestFixture modulator("FMOperator");
    modulator.set("frequency", static_cast<float>(kHz));
    modulator.set("ratio", 0.25f);
    const auto driven = modulator.run(kSilence);

    ElementTestFixture carrier("FMOperator");
    carrier.set("frequency", static_cast<float>(kHz));
    carrier.set("depth", 2.0f);
    const auto separate = carrier.run(driven);

    ElementTestFixture matrix("FMMatrix");
    matrix.set("frequency", static_cast<float>(kHz));
    matrix.set("depth", 2.0f);
    matrix.set("ratio2", 0.25f);
    matrix.set("m12", 1.0f);
    matrix.set("carrier1", 1.0f);
    assert(largestDifference(matrix.run(kSilence), separate) < 1.0e-4f);

    // And its own feedback is the operator's.
    ElementTestFixture selfFed("FMOperator");
    selfFed.set("frequency", static_cast<float>(kHz));
    selfFed.set("feedback", 7.0f);

    ElementTestFixture selfFedMatrix("FMMatrix");
    selfFedMatrix.set("frequency", static_cast<float>(kHz));
    selfFedMatrix.set("m11", 7.0f);
    selfFedMatrix.set("carrier1", 1.0f);
    assert(largestDifference(selfFedMatrix.run(kSilence), selfFed.run(kSilence)) < 1.0e-4f);
}

/// An algorithm number sets the matrix and carriers the DX7 chart gives it.
void testAlgorithms()
{
    // Algorithm 5 is three pairs: 2 into 1, 4 into 3, 6 into 5, all three pairs heard.
    const auto configure = [](ElementTestFixture& rig)
    {
        rig.set("frequency", static_cast<float>(kHz));
        rig.set("depth", 2.0f);
        for (const char* ratio : { "ratio2", "ratio4", "ratio6" })
            rig.set(ratio, 2.0f);
    };

    ElementTestFixture byNumber("FMMatrix");
    configure(byNumber);
    byNumber.set("algorithm", 5.0f);

    ElementTestFixture byHand("FMMatrix");
    configure(byHand);
    byHand.set("m12", 1.0f);
    byHand.set("m34", 1.0f);
    byHand.set("m56", 1.0f);
    for (const char* carrier : { "carrier1", "carrier3", "carrier5" })
        byHand.set(carrier, 1.0f);
    assert(largestDifference(byNumber.run(kSilence), byHand.run(kSilence)) < 1.0e-4f);

    // The DX7's feedback control reaches the operator the algorithm gives it to: 6 in
    // algorithm 5, and the sound changes with it.
    ElementTestFixture fed("FMMatrix");
    configure(fed);
    fed.set("algorithm", 5.0f);
    fed.set("feedback", 7.0f);
    ElementTestFixture fedByHand("FMMatrix");
    configure(fedByHand);
    for (const char* name : { "m12", "m34", "m56" })
        fedByHand.set(name, 1.0f);
    for (const char* carrier : { "carrier1", "carrier3", "carrier5" })
        fedByHand.set(carrier, 1.0f);
    fedByHand.set("m66", 7.0f);
    assert(largestDifference(fed.run(kSilence), fedByHand.run(kSilence)) < 1.0e-4f);

    // With no modulation every carrier is the same sine, so the peak counts them.
    // The DX7 chart gives these numbers of carriers.
    const int carriers[32] = { 2, 2, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
                               1, 1, 3, 3, 4, 4, 4, 5, 5, 3, 3, 3, 4, 4, 5, 6 };
    for (int algorithm = 1; algorithm <= 32; ++algorithm)
    {
        ElementTestFixture rig("FMMatrix");
        rig.set("frequency", static_cast<float>(kHz));
        rig.set("depth", 0.0f);
        rig.set("algorithm", static_cast<float>(algorithm));
        const float peak = largestDifference(rig.run(kSilence), kSilence);

        ElementTestFixture one("FMMatrix");
        one.set("frequency", static_cast<float>(kHz));
        one.set("carrier1", 1.0f);
        const float single = largestDifference(one.run(kSilence), kSilence);

        const int counted = static_cast<int>(std::lround(peak / single));
        if (counted != carriers[algorithm - 1]) std::printf("  algorithm %d: counted %d carriers, chart says %d\n", algorithm, counted, carriers[algorithm - 1]);
        assert(counted == carriers[algorithm - 1]);
    }

    // Every algorithm, with feedback at its strongest and modulation at its default, is
    // finite: the loops through several operators (4 and 6) included.
    for (int algorithm = 1; algorithm <= 32; ++algorithm)
    {
        ElementTestFixture rig("FMMatrix");
        rig.set("frequency", static_cast<float>(kHz));
        rig.set("algorithm", static_cast<float>(algorithm));
        rig.set("feedback", 7.0f);
        for (const float v : rig.run(kSilence))
            assert(std::isfinite(v) && std::abs(v) <= 8.0f);
    }

    // A loop through three operators is not the same sound as the same algorithm without it.
    ElementTestFixture looped("FMMatrix"), open("FMMatrix");
    for (auto* rig : { &looped, &open })
    {
        rig->set("frequency", static_cast<float>(kHz));
        rig->set("algorithm", 4.0f);
    }
    looped.set("feedback", 7.0f);
    assert(largestDifference(looped.run(kSilence), open.run(kSilence)) > 0.05f);
}

/// Operators 1 and 2 modulate each other, which no arrangement of separate
/// elements can do within a block.
void testMatrixLoopSpansOperators()
{
    const auto make = [](float back)
    {
        auto rig = std::make_unique<ElementTestFixture>("FMMatrix");
        rig->set("frequency", static_cast<float>(kHz));
        rig->set("ratio2", 2.0f);
        rig->set("m12", 0.3f);
        rig->set("m21", back);
        rig->set("carrier1", 1.0f);
        return rig;
    };

    const auto open = make(0.0f)->run(kSilence);
    const auto closed = make(0.3f)->run(kSilence);

    // The return path changes the sound, and it stays bounded and finite.
    assert(largestDifference(open, closed) > 0.05f);
    double spectral = 0.0;
    for (int k = 1; k <= 8; ++k)
        spectral = std::max(spectral, std::abs(amplitudeAt(closed, k * kHz) - amplitudeAt(open, k * kHz)));
    assert(spectral > 0.02);
    for (const float v : closed)
        assert(std::isfinite(v) && std::abs(v) <= 1.0f);

    // The loop is closed a sample at a time, so how the host splits the
    // signal into blocks cannot show in the result.
    auto split = make(0.3f);
    std::vector<float> pieces;
    for (int start = 0; start < kSamples; start += 480)
    {
        const auto piece = split->run(std::vector<float>(480, 0.0f));
        pieces.insert(pieces.end(), piece.begin(), piece.end());
    }
    assert(largestDifference(closed, pieces) < 1.0e-5f);
}

void testMatrixSilence()
{
    ElementTestFixture quiet("FMMatrix");
    quiet.set("carrier1", 1.0f);
    for (int i = 1; i <= 6; ++i)
        quiet.set("amp" + std::to_string(i), 0.0f);
    assert(peak(quiet.run(kSilence)) == 0.0f);

    // A modulator that nothing hears makes no sound.
    ElementTestFixture modulatorOnly("FMMatrix");
    assert(peak(modulatorOnly.run(kSilence)) == 0.0f);
}

/// Runs the envelope for `seconds` a control slice at a time, as the engine
/// does, and returns the last value.
float runEnvelope(ElementTestFixture& rig, double seconds)
{
    const std::vector<float> slice(32, 0.0f);
    float last = 0.0f;
    for (int i = 0; i < static_cast<int>(seconds * kRate / 32.0); ++i)
    {
        rig.run(slice);
        last = rig.lastControlOut[0];
    }
    return last;
}

void testEnvelopeSegments()
{
    ElementTestFixture rig("RateLevelEnvelope");
    rig.set("l3", 79.0f);                            // 20 steps down: 2^-2.5
    rig.setNote(60, 1.0f, false);
    assert(runEnvelope(rig, 0.01) == 0.0f);          // rests at L4

    rig.setNote(60, 1.0f, true);
    assert(runEnvelope(rig, 0.05) > 0.0f);
    const float sustained = runEnvelope(rig, 0.5);
    assert(std::abs(sustained - std::pow(2.0f, -2.5f)) < 0.005f);
    assert(std::abs(runEnvelope(rig, 0.5) - sustained) < 1.0e-6f);    // and holds

    rig.setNote(60, 1.0f, false);
    assert(runEnvelope(rig, 0.2) < 1.0e-3f);
}

void testRateIsASpeed()
{
    // A slow rate must take longer than a fast one over the same distance.
    ElementTestFixture slow("RateLevelEnvelope"), fast("RateLevelEnvelope");
    slow.set("r1", 40.0f);
    fast.set("r1", 80.0f);
    slow.setNote(60, 1.0f, true);
    fast.setNote(60, 1.0f, true);

    const float s = runEnvelope(slow, 0.05);
    const float f = runEnvelope(fast, 0.05);
    assert(f > 0.9f);
    assert(s < f);

    ElementTestFixture crawl("RateLevelEnvelope");
    crawl.set("r4", 0.0f);
    crawl.setNote(60, 1.0f, true);
    runEnvelope(crawl, 0.1);
    crawl.setNote(60, 1.0f, false);
    assert(runEnvelope(crawl, 0.5) > 0.8f);           // forty seconds across the scale
}

void testEnvelopeStagesRun()
{
    // L1 low then L2 high then L3 middle: the level must visit each in order.
    ElementTestFixture rig("RateLevelEnvelope");
    rig.set("l1", 50.0f);
    rig.set("l2", 99.0f);
    rig.set("l3", 70.0f);
    rig.set("r1", 90.0f);
    rig.set("r2", 60.0f);
    rig.set("r3", 50.0f);
    rig.setNote(60, 1.0f, true);

    float high = 0.0f;
    for (int i = 0; i < 4000; ++i)
    {
        rig.run(std::vector<float>(32, 0.0f));
        high = std::max(high, rig.lastControlOut[0]);
    }
    assert(high > 0.99f);                             // reached L2
    assert(std::abs(rig.lastControlOut[0] - std::pow(2.0f, -29.0f / 8.0f)) < 0.002f);
}

/// Slices until the level first falls below a fraction of where it started: a measure of
/// how fast the second segment runs.
int slicesToFall(int note, float rateScale)
{
    ElementTestFixture rig("RateLevelEnvelope");
    rig.set("l1", 99.0f);
    rig.set("l2", 0.0f);
    rig.set("r1", 99.0f);
    rig.set("r2", 40.0f);
    rig.set("ratescale", rateScale);
    rig.setNote(note, 1.0f, true);

    for (int slice = 0; slice < 100000; ++slice)
    {
        rig.run(std::vector<float>(32, 0.0f));
        if (slice > 5 && rig.lastControlOut[0] < 0.05f)
            return slice;
    }
    return 100000;
}

void testKeyboardRateScaling()
{
    // Without it the note does not matter. With it, a high key's decay is shorter than a low one's.
    assert(slicesToFall(36, 0.0f) == slicesToFall(96, 0.0f));
    const int low = slicesToFall(36, 7.0f), high = slicesToFall(96, 7.0f);
    std::printf("  rate scaling 7: decay takes %d slices from note 36, %d from note 96\n", low, high);
    assert(high * 3 < low);
}

void testKeyboardLevelScaling()
{
    const auto level = [](int note, float left, float right)
    {
        ElementTestFixture rig("RateLevelEnvelope");
        rig.set("breakpoint", 60.0f);
        rig.set("leftdepth", left);
        rig.set("rightdepth", right);
        rig.setNote(note, 1.0f, true);
        for (int i = 0; i < 50; ++i)
            rig.run(std::vector<float>(32, 0.0f));
        return rig.lastControlOut[0];
    };

    // At the breakpoint nothing changes; a negative right depth quietens the keys above it
    // and leaves the ones below; a positive left depth cannot go past full level.
    assert(std::abs(level(60, -50.0f, -50.0f) - level(60, 0.0f, 0.0f)) < 1.0e-5f);
    assert(level(96, 0.0f, -50.0f) < level(96, 0.0f, 0.0f) * 0.1f);
    assert(std::abs(level(24, 0.0f, -50.0f) - level(24, 0.0f, 0.0f)) < 1.0e-5f);
    assert(level(24, 30.0f, 0.0f) <= 1.0f + 1.0e-5f);
    // Halfway out the effect is half of the full depth: 18 semitones is half of 36.
    const float full = level(96, 0.0f, -40.0f), half = level(78, 0.0f, -40.0f);
    assert(half > full && half < 1.0f);
}

void testPitchEnvelopeRatio()
{
    ElementTestFixture rig("RateLevelEnvelope");
    rig.set("pitchrange", 1.0f);
    rig.set("l1", 99.0f);
    rig.set("l2", 99.0f);
    rig.set("l3", 99.0f);
    rig.set("l4", 50.0f);
    rig.set("r1", 99.0f);

    // At rest on L4 = 50 the ratio is 1.
    rig.run(std::vector<float>(32, 0.0f));
    assert(std::abs(rig.lastControlOut[1] - 1.0f) < 1.0e-5f);

    // Struck, it goes to L1 = 99, almost an octave up.
    rig.setNote(60, 1.0f, true);
    for (int i = 0; i < 50; ++i)
        rig.run(std::vector<float>(32, 0.0f));
    assert(std::abs(rig.lastControlOut[1] - std::exp2(0.98f)) < 0.01f);

    // With the range at its default there is no pitch movement at all.
    ElementTestFixture flat("RateLevelEnvelope");
    flat.setNote(60, 1.0f, true);
    for (int i = 0; i < 50; ++i)
        flat.run(std::vector<float>(32, 0.0f));
    assert(flat.lastControlOut[1] == 1.0f);
}

void testVelocitySensitivity()
{
    ElementTestFixture rig("RateLevelEnvelope");
    rig.set("velsens", 7.0f);
    rig.set("velocity", 0.0f);
    rig.setNote(60, 1.0f, true);
    assert(std::abs(runEnvelope(rig, 0.1) - std::pow(2.0f, -7.0f)) < 1.0e-3f);

    rig.set("velocity", 1.0f);
    assert(runEnvelope(rig, 0.01) > 0.99f);

    ElementTestFixture insensitive("RateLevelEnvelope");
    insensitive.set("velocity", 0.0f);
    insensitive.setNote(60, 1.0f, true);
    assert(runEnvelope(insensitive, 0.1) > 0.99f);
}

}  // namespace

int main()
{
    testPlainSine();
    testLevelIsThreeQuartersOfADecibelAStep();
    testModulationSidebands();
    testFeedbackAddsHarmonics();
    testOperatorIsDeterministic();
    testMatrixMatchesSeparateOperators();
    testAlgorithms();
    testMatrixLoopSpansOperators();
    testMatrixSilence();
    testEnvelopeSegments();
    testRateIsASpeed();
    testEnvelopeStagesRun();
    testVelocitySensitivity();
    testKeyboardRateScaling();
    testKeyboardLevelScaling();
    testPitchEnvelopeRatio();

    std::puts("FMTest passed");
    return 0;
}

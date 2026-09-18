// tests/dsp/GranulatorTest.cpp
//
// val:Granulator, val:Transport and val:MidiInterval. The granulator is the
// only element that randomises anything, so the first thing the tests establish
// is that it is reproducible: a render that changed run to run could not be
// checked at all.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

/// A frequency with a whole number of periods in `n` samples, so material
/// recorded into the buffer joins seamlessly where a grain wraps around it.
double exactFrequency(int periods, int n)
{
    return kRate * static_cast<double>(periods) / static_cast<double>(n);
}

/// Records `n` samples of a sine into the granulator's buffer, then freezes it.
void fillBuffer(ElementTestFixture& rig, double frequency, int n)
{
    rig.set("freeze", 0.0f);     // record
    rig.set("density", 0.01f);   // and stay out of the way while recording
    rig.run(ElementTestFixture::generateSine(frequency, kRate, n));
    rig.set("freeze", 1.0f);
}

void testRecordsAndPlaysGrains()
{
    ElementTestFixture rig("Granulator", kRate);

    constexpr int n = 8192;
    fillBuffer(rig, exactFrequency(84, n), n);

    rig.set("density", 30.0f);
    rig.set("size", 100.0f);
    rig.set("spread", 0.0f);

    const std::vector<float> silence(n, 0.0f);
    const auto left  = rig.run(silence, "left");

    // Frozen, with a silent input, everything heard comes from the buffer.
    assert(ElementTestFixture::measureRms(left) > 0.01f);

    for (float sample : left)
        assert(std::isfinite(sample));
}

/// Every declared output carries signal, and the mono output is the sum of the
/// two sides rather than an unwritten buffer.
void testWritesAllThreeOutputs()
{
    ElementTestFixture rig("Granulator", kRate);

    constexpr int n = 8192;
    fillBuffer(rig, exactFrequency(84, n), n);

    rig.set("density", 40.0f);
    rig.set("spread", 1.0f);

    const std::vector<float> silence(n, 0.0f);

    // Each run advances the element, so the three outputs are read from one
    // run by re-running with the same state: instead, check them in turn over
    // successive blocks, which is enough to prove none is left untouched.
    assert(ElementTestFixture::measureRms(rig.run(silence, "out"))   > 0.001f);
    assert(ElementTestFixture::measureRms(rig.run(silence, "left"))  > 0.001f);
    assert(ElementTestFixture::measureRms(rig.run(silence, "right")) > 0.001f);
}

/// A frozen granulator ignores its input. An unfrozen one records over the
/// buffer, so the same read position eventually returns the new material.
void testFreezeStopsRecording()
{
    ElementTestFixture frozen("Granulator", kRate);
    ElementTestFixture recording("Granulator", kRate);

    constexpr int n = 8192;
    const double frequency = exactFrequency(84, n);

    // A buffer just long enough for the material, so recording over it takes a
    // few blocks rather than the eight seconds the default would need.
    std::string error;
    assert(frozen.element->setOption("seconds", "0.1707", error));
    assert(recording.element->setOption("seconds", "0.1707", error));

    fillBuffer(frozen, frequency, n);
    fillBuffer(recording, frequency, n);
    recording.set("freeze", 0.0f);

    frozen.set("density", 20.0f);
    recording.set("density", 20.0f);

    // Both now see silence. The frozen one keeps its material; the recording
    // one writes silence over it and fades out.
    const std::vector<float> silence(n, 0.0f);
    float frozenLevel = 0.0f, recordedLevel = 0.0f;
    for (int block = 0; block < 4; ++block)
    {
        frozenLevel   = ElementTestFixture::measureRms(frozen.run(silence, "left"));
        recordedLevel = ElementTestFixture::measureRms(recording.run(silence, "left"));
    }

    assert(frozenLevel > 0.01f);
    assert(recordedLevel < frozenLevel * 0.1f);
}

/// Record follows the input, Freeze stops following it. The test records low
/// material, switches the control, then feeds high material: what comes out
/// tells the two apart, because only one of them takes on the new sound.
///
/// It takes a few blocks. At position 0 a grain reads forward from the write
/// head, which is the oldest sample in the buffer, so what is heard is always
/// about one buffer behind what is arriving.
void testFreezeHoldsWhileRecordFollows()
{
    constexpr int blockSize = 8192;

    auto centroidAfterHighMaterial = [&](float freeze)
    {
        ElementTestFixture rig("Granulator", kRate);

        // A buffer shorter than the material, so it holds a window onto it.
        std::string error;
        assert(rig.element->setOption("seconds", "0.1707", error));

        rig.set("density", 60.0f);
        rig.set("size", 40.0f);
        rig.set("spread", 0.0f);
        rig.set("shape", 1.0f);
        rig.set("freeze", 0.0f);

        double phase = 0.0;
        auto tone = [&](double hz)
        {
            std::vector<float> out(static_cast<std::size_t>(blockSize));
            for (int i = 0; i < blockSize; ++i)
            {
                phase += 2.0 * M_PI * hz / kRate;
                out[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(phase));
            }
            return out;
        };

        rig.run(tone(400.0));
        rig.set("freeze", freeze);

        std::vector<float> out;
        for (int block = 0; block < 5; ++block)
            out = rig.run(tone(2400.0), "left");

        const auto bins = ElementTestFixture::computeSpectrum(out, 12);
        double weighted = 0.0, total = 0.0;
        for (std::size_t b = 1; b < bins.size(); ++b)
        {
            const double energy = static_cast<double>(bins[b]) * bins[b];
            weighted += energy * static_cast<double>(b);
            total    += energy;
        }
        return total > 0.0 ? weighted / total * kRate / 4096.0 : 0.0;
    };

    const double recording = centroidAfterHighMaterial(0.0f);
    const double frozen    = centroidAfterHighMaterial(1.0f);

    std::printf("  freeze: centroid %.0f Hz recording, %.0f Hz frozen\n", recording, frozen);

    assert(recording > 1500.0);   // taken on the new material
    assert(frozen    <  900.0);   // still playing what it captured
}

/// Two elements built the same way must render the same samples, or no audio
/// test of this element means anything.
void testRenderIsReproducible()
{
    constexpr int n = 4096;
    const double frequency = exactFrequency(42, n);

    auto render = [&]
    {
        ElementTestFixture rig("Granulator", kRate);
        fillBuffer(rig, frequency, n);
        rig.set("density", 25.0f);
        rig.set("spray", 0.6f);
        rig.set("jitter", 0.8f);
        rig.set("pitchJitter", 5.0f);
        rig.set("spread", 1.0f);
        rig.set("reverse", 0.4f);
        return rig.run(std::vector<float>(n, 0.0f), "left");
    };

    const auto first  = render();
    const auto second = render();

    assert(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
        assert(first[i] == second[i]);
}

/// Grain onsets are driven by the sample stream, never by the index within the
/// current block, so the host's buffer size must not change the output. This is
/// the failure the SignalGenerator impulse had; see MISTAKES.md.
void testBlockSizeIndependence()
{
    constexpr int n = 16384;
    const double frequency = exactFrequency(168, n);

    auto render = [&](int blockSize)
    {
        ElementTestFixture rig("Granulator", kRate);
        fillBuffer(rig, frequency, n);

        // Enough onsets for the randomised interval to be exercised many times,
        // and a drifting read point, since both are carried between blocks.
        rig.set("density", 80.0f);
        rig.set("size", 60.0f);
        rig.set("jitter", 0.8f);
        rig.set("spray", 0.3f);
        rig.set("scan", 0.7f);

        std::vector<float> all;
        for (int at = 0; at < n; at += blockSize)
        {
            const auto block = rig.run(std::vector<float>(static_cast<std::size_t>(blockSize), 0.0f), "left");
            all.insert(all.end(), block.begin(), block.end());
        }
        return all;
    };

    const auto small = render(64);
    const auto large = render(512);

    assert(small.size() == large.size());
    for (std::size_t i = 0; i < small.size(); ++i)
        assert(std::abs(small[i] - large[i]) < 1.0e-6f);
}

/// Transposing by an octave puts the energy an octave up. One long grain with
/// no randomisation, so what comes out is the buffer read at a known rate.
void testPitchTransposes()
{
    constexpr int n = 8192;
    const double frequency = exactFrequency(84, n);   // 492.1875 Hz

    auto energyRatio = [&](float semitones)
    {
        ElementTestFixture rig("Granulator", kRate);
        fillBuffer(rig, frequency, n);

        rig.set("density", 40.0f);
        rig.set("size", 250.0f);
        rig.set("shape", 1.0f);
        rig.set("spread", 0.0f);
        rig.set("pitch", semitones);

        // The second block, by which time the cloud is running steadily.
        const std::vector<float> silence(n, 0.0f);
        rig.run(silence, "left");
        const auto out = rig.run(silence, "left");
        const auto bins = ElementTestFixture::computeSpectrum(out, 12);

        const float atRoot   = ElementTestFixture::measureEnergyAt(bins, frequency, kRate);
        const float atOctave = ElementTestFixture::measureEnergyAt(bins, frequency * 2.0, kRate);
        return atOctave / std::max(atRoot, 1.0e-12f);
    };

    const float unshifted = energyRatio(0.0f);
    const float octaveUp  = energyRatio(12.0f);

    std::printf("  pitch: octave/root energy %.4f unshifted, %.4f at +12\n", unshifted, octaveUp);

    assert(unshifted < 0.1f);
    assert(octaveUp > 10.0f);
}

/// A sound file is a resource with an identity, not just a path. A circuit may
/// declare what the file it names is, and a file that is not that fails the
/// load with a message rather than playing something else.
void testDeclaredResourceIdentityIsChecked()
{
    const char* kBell = VALIS_EXAMPLES_DIR "/samples/bell.wav";
    const char* kHash = "694a657853b352b611868fa31227a3ded425efc094542c9f76ee77275dccf507";

    // The real hash and dimensions load, whichever order they arrive in: RDF
    // states no order, so the check must not depend on one.
    {
        ElementTestFixture rig("SampleLoad", kRate);
        std::string error;
        assert(rig.element->setOption("file", kBell, error));
        assert(rig.element->setOption("sha256", kHash, error));
        assert(rig.element->setOption("channels", "1", error));
        assert(rig.element->setOption("sampleRate", "32000", error));
        assert(rig.element->setOption("frames", "83200", error));
        assert(error.empty());
    }
    {
        ElementTestFixture rig("SampleLoad", kRate);
        std::string error;
        assert(rig.element->setOption("sha256", kHash, error));
        assert(rig.element->setOption("frames", "83200", error));
        assert(rig.element->setOption("file", kBell, error));
        assert(error.empty());
    }

    // A hash that does not match the file names both, so the user can see
    // which file they actually have.
    {
        ElementTestFixture rig("SampleLoad", kRate);
        std::string error;
        const char* wrong = "0000000000000000000000000000000000000000000000000000000000000000";
        assert(rig.element->setOption("sha256", wrong, error));
        assert(! rig.element->setOption("file", kBell, error));
        assert(error.find("val:sha256") != std::string::npos);
        assert(error.find(kHash) != std::string::npos);
    }

    // Declared after the file, the mismatch is caught just the same.
    {
        ElementTestFixture rig("SampleLoad", kRate);
        std::string error;
        assert(rig.element->setOption("file", kBell, error));
        assert(! rig.element->setOption("frames", "12345", error));
        assert(error.find("val:frames") != std::string::npos);
    }

    // Wrong dimensions are caught individually.
    {
        ElementTestFixture rig("SampleLoad", kRate);
        std::string error;
        assert(rig.element->setOption("file", kBell, error));
        assert(! rig.element->setOption("channels", "2", error));
        assert(error.find("val:channels") != std::string::npos);
    }
    {
        ElementTestFixture rig("SampleLoad", kRate);
        std::string error;
        assert(rig.element->setOption("file", kBell, error));
        assert(! rig.element->setOption("sampleRate", "44100", error));
        assert(error.find("val:sampleRate") != std::string::npos);
    }

    // A malformed declaration is a failure, not a silent "not declared": a
    // mistyped hash that was ignored would give false confidence.
    {
        ElementTestFixture rig("SampleLoad", kRate);
        std::string error;
        assert(! rig.element->setOption("sha256", "deadbeef", error));
        assert(error.find("64 hexadecimal") != std::string::npos);
    }

    // The granulator reads files through the same path, so it checks too.
    {
        ElementTestFixture rig("Granulator", kRate);
        std::string error;
        assert(rig.element->setOption("sha256", kHash, error));
        assert(rig.element->setOption("file", kBell, error));
        assert(error.empty());
    }

    // Declaring nothing still loads, which is what every existing circuit does.
    {
        ElementTestFixture rig("SampleLoad", kRate);
        std::string error;
        assert(rig.element->setOption("file", kBell, error));
        assert(error.empty());
    }
}

/// A sample file that is not there fails the option, and the engine turns that
/// into a load error rather than an element that quietly plays nothing.
void testMissingFileIsAnError()
{
    ElementTestFixture rig("Granulator", kRate);

    std::string error;
    assert(! rig.element->setOption("file", "no/such/sample.wav", error));
    assert(! error.empty());

    // An unrecognised key is not a failure.
    std::string ignored;
    assert(rig.element->setOption("nonsense", "1", ignored));
    assert(ignored.empty());
}

void testTransportPhaseAndPulse()
{
    ElementTestFixture rig("Transport", kRate);
    rig.set("division", 1.0f);          // one quarter note

    const std::vector<float> block(32, 0.0f);

    // Stopped: the phase free-runs at the tempo, and no host position is used.
    rig.setTransport(false, 120.0, 0.0);
    rig.run(block);
    assert(rig.lastControlOut[0] == 0.0f);              // playing
    assert(std::abs(rig.lastControlOut[1] - 120.0f) < 1.0e-3f);   // tempo
    assert(std::abs(rig.lastControlOut[4] - 2.0f) < 1.0e-3f);     // 120 bpm = 2 Hz

    const float freeRunning = rig.lastControlOut[2];
    rig.run(block);
    assert(rig.lastControlOut[2] > freeRunning);

    // Playing: the phase follows the host's position within the division.
    rig.setTransport(true, 120.0, 0.0);
    rig.run(block);
    assert(rig.lastControlOut[0] == 1.0f);
    assert(rig.lastControlOut[3] == 1.0f);              // the downbeat is a pulse
    assert(std::abs(rig.lastControlOut[2]) < 1.0e-6f);

    rig.setTransport(true, 120.0, 0.25);
    rig.run(block);
    assert(std::abs(rig.lastControlOut[2] - 0.25f) < 1.0e-5f);
    assert(rig.lastControlOut[3] == 0.0f);              // still inside the beat

    rig.setTransport(true, 120.0, 1.0);
    rig.run(block);
    assert(rig.lastControlOut[3] == 1.0f);              // next beat
    assert(std::abs(rig.lastControlOut[2]) < 1.0e-6f);

    // A division of a sixteenth pulses four times as often.
    rig.set("division", 0.25f);
    rig.setTransport(true, 120.0, 1.25);
    rig.run(block);
    assert(rig.lastControlOut[3] == 1.0f);
    assert(std::abs(rig.lastControlOut[4] - 8.0f) < 1.0e-3f);
}

/// The trigger port replaces free-running onsets: one grain per rising edge.
void testExternalTriggerFiresGrains()
{
    ElementTestFixture rig("Granulator", kRate);

    constexpr int n = 4096;
    fillBuffer(rig, exactFrequency(42, n), n);

    rig.set("size", 20.0f);
    rig.set("spread", 0.0f);
    rig.set("trigger", 0.0f);      // external, resting low

    const std::vector<float> silence(512, 0.0f);

    // No edge, no grain, whatever the density says. The grain the element
    // fired while it was still free-running has to finish first.
    rig.set("density", 100.0f);
    for (int block = 0; block < 24; ++block)
        rig.run(silence, "left");
    assert(ElementTestFixture::measureRms(rig.run(silence, "left")) == 0.0f);

    rig.set("trigger", 1.0f);
    const auto fired = rig.run(silence, "left");
    assert(ElementTestFixture::measureRms(fired) > 0.001f);

    // Holding the trigger high does not retrigger: the grain ends and the
    // element goes quiet again.
    for (int block = 0; block < 4; ++block)
        rig.run(silence, "left");
    assert(ElementTestFixture::measureRms(rig.run(silence, "left")) == 0.0f);
}

void testMidiInterval()
{
    ElementTestFixture rig("MidiInterval", kRate);
    rig.set("root", 60.0f);

    rig.setNote(72);
    rig.run(std::vector<float>(32, 0.0f));
    assert(std::abs(rig.lastControlOut[0] - 12.0f) < 1.0e-5f);
    assert(std::abs(rig.lastControlOut[1] - 2.0f) < 1.0e-5f);

    rig.setNote(48);
    rig.run(std::vector<float>(32, 0.0f));
    assert(std::abs(rig.lastControlOut[0] + 12.0f) < 1.0e-5f);
    assert(std::abs(rig.lastControlOut[1] - 0.5f) < 1.0e-5f);

    rig.setNote(60);
    rig.run(std::vector<float>(32, 0.0f));
    assert(std::abs(rig.lastControlOut[0]) < 1.0e-5f);
    assert(std::abs(rig.lastControlOut[1] - 1.0f) < 1.0e-5f);
}

/// val:SampleLoad reads a file and plays it. The file it is given here is the
/// one the granular example ships with.
void testSampleLoadPlaysAFile()
{
    ElementTestFixture rig("SampleLoad", kRate);

    std::string error;
    const bool loaded = rig.element->setOption("file", VALIS_EXAMPLES_DIR "/samples/bell.wav", error);
    if (! loaded)
        std::printf("  SampleLoad: %s\n", error.c_str());
    assert(loaded);
    assert(error.empty());

    const std::vector<float> silence(4096, 0.0f);
    const auto out = rig.run(silence);

    assert(ElementTestFixture::measureRms(out) > 0.01f);
    for (float sample : out)
        assert(std::isfinite(sample));

    // Nothing to read is silence, not noise.
    ElementTestFixture empty("SampleLoad", kRate);
    assert(ElementTestFixture::measurePeak(empty.run(silence)) == 0.0f);

    std::string missing;
    assert(! empty.element->setOption("file", "no/such/sample.wav", missing));
    assert(! missing.empty());
}

/// Looping wraps back to the start; one-shot stops at the end and stays stopped
/// until a trigger restarts it.
void testSampleLoadLoopAndOneShot()
{
    // The shipped sample is 2.6 seconds, so six of these blocks run well past
    // its end at the engine's rate.
    const int blocks = 6;
    const int blockSize = 32768;

    auto levelAfterPlayingPast = [&](float loop)
    {
        ElementTestFixture rig("SampleLoad", kRate);
        std::string error;
        assert(rig.element->setOption("file", VALIS_EXAMPLES_DIR "/samples/bell.wav", error));
        rig.set("loop", loop);

        const std::vector<float> silence(static_cast<std::size_t>(blockSize), 0.0f);
        float last = 0.0f;
        for (int i = 0; i < blocks; ++i)
            last = ElementTestFixture::measureRms(rig.run(silence));
        return last;
    };

    assert(levelAfterPlayingPast(1.0f) > 0.01f);    // still going round
    assert(levelAfterPlayingPast(0.0f) == 0.0f);    // played once and stopped
}

/// val:Select passes one of two values, and repeats the switch position on thru
/// so one switch can drive more than one destination.
void testSelectSwitchesBetweenTwoValues()
{
    ElementTestFixture rig("Select", kRate);
    rig.set("a", 1.0f);
    rig.set("b", -1.0f);

    const std::vector<float> block(32, 0.0f);

    rig.set("select", 0.0f);
    rig.run(block);
    assert(rig.lastControlOut[0] == 1.0f);
    assert(rig.lastControlOut[1] == 0.0f);

    rig.set("select", 1.0f);
    rig.run(block);
    assert(rig.lastControlOut[0] == -1.0f);
    assert(rig.lastControlOut[1] == 1.0f);
}

}  // namespace

int main()
{
    testRecordsAndPlaysGrains();
    testWritesAllThreeOutputs();
    testFreezeStopsRecording();
    testFreezeHoldsWhileRecordFollows();
    testRenderIsReproducible();
    testBlockSizeIndependence();
    testPitchTransposes();
    testDeclaredResourceIdentityIsChecked();
    testMissingFileIsAnError();
    testTransportPhaseAndPulse();
    testExternalTriggerFiresGrains();
    testMidiInterval();
    testSampleLoadPlaysAFile();
    testSampleLoadLoopAndOneShot();
    testSelectSwitchesBetweenTwoValues();

    std::printf("GranulatorTest PASSED\n");
    return 0;
}

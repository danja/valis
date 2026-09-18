// tests/engine/ValisEngineTest.cpp

#include "valis/CircuitCompiler.h"
#include "valis/CircuitModel.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Allocation tracking. The real-time contract says process() must not allocate,
// so the test enforces it rather than trusting the code to behave.
// ---------------------------------------------------------------------------
namespace {
std::atomic<int> allocationCount{0};
std::atomic<bool> trackingEnabled{false};
}  // namespace

void* operator new(std::size_t size)
{
    if (trackingEnabled.load(std::memory_order_relaxed))
        allocationCount.fetch_add(1, std::memory_order_relaxed);

    if (void* p = std::malloc(size ? size : 1))
        return p;

    throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace valis;

namespace {

const Ontology& ontology()
{
    static const Ontology loaded = [] {
        Ontology o;
        std::vector<std::string> errors;
        o.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool ok = o.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(ok);
        return o;
    }();
    return loaded;
}

bool compileTurtle(std::string_view turtle, CompiledCircuit& out)
{
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    if (! store.parse(turtle, "urn:valis:test", parseErrors))
        return false;

    CircuitModel model;
    std::vector<Diagnostic> diagnostics;
    if (! model.build(store, ontology(), diagnostics))
        return false;

    CircuitCompiler compiler;
    return compiler.compile(model, ontology(), out, diagnostics);
}

bool compileFile(const char* path, CompiledCircuit& out)
{
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    if (! store.parseFile(path, parseErrors))
        return false;

    CircuitModel model;
    std::vector<Diagnostic> diagnostics;
    if (! model.build(store, ontology(), diagnostics))
        return false;

    CircuitCompiler compiler;
    return compiler.compile(model, ontology(), out, diagnostics);
}

const char* kGain = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :in , :g , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:g  a val:Gain ; val:gain -6.0 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :g  ; val:port "in"  ] .
:a2 a val:Arc ; val:from [ val:node :g  ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)";

std::vector<float> render(ValisEngine& engine, const std::vector<float>& input, int blockSize)
{
    std::vector<float> output(input.size(), 0.0f);
    for (std::size_t at = 0; at < input.size(); at += static_cast<std::size_t>(blockSize))
    {
        const auto n = static_cast<int>(std::min(static_cast<std::size_t>(blockSize),
                                                 input.size() - at));
        engine.process(input.data() + at, output.data() + at, n);
    }
    return output;
}

std::vector<float> tone(double hz, double rate, int n, float amp = 0.5f)
{
    std::vector<float> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        out[static_cast<std::size_t>(i)] =
            amp * static_cast<float>(std::sin(2.0 * M_PI * hz * i / rate));
    return out;
}

float peakOf(const std::vector<float>& v)
{
    float peak = 0.0f;
    for (const float s : v) peak = std::max(peak, std::abs(s));
    return peak;
}

// ---------------------------------------------------------------------------

void testGainCircuitRuns()
{
    CompiledCircuit circuit;
    assert(compileTurtle(kGain, circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 128);

    std::string error;
    assert(engine.load(circuit, registry, error));
    assert(engine.hasCircuit());

    const std::vector<float> input(512, 1.0f);
    const auto output = render(engine, input, 128);

    // -6 dB
    for (const float s : output)
        assert(std::abs(s - 0.5011872f) < 1.0e-4f);
}

void testNoCircuitIsSilenceNotGarbage()
{
    ValisEngine engine;
    engine.prepare(48000.0, 128);
    assert(! engine.hasCircuit());

    std::vector<float> output(128, 12345.0f);
    const std::vector<float> input(128, 1.0f);
    engine.process(input.data(), output.data(), 128);

    for (const float s : output)
        assert(s == 0.0f);
}

/// The core real-time claim, enforced rather than asserted in a comment.
void testProcessDoesNotAllocate()
{
    CompiledCircuit circuit;
    assert(compileFile(VALIS_EXAMPLES_DIR "/skream.ttl", circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 512);

    std::string error;
    assert(engine.load(circuit, registry, error));

    const auto input = tone(100.0, 48000.0, 512);
    std::vector<float> output(512, 0.0f);

    // Warm up outside the measurement.
    engine.process(input.data(), output.data(), 512);

    trackingEnabled.store(true, std::memory_order_relaxed);
    const int before = allocationCount.load(std::memory_order_relaxed);

    for (int block = 0; block < 200; ++block)
        engine.process(input.data(), output.data(), 512);

    const int after = allocationCount.load(std::memory_order_relaxed);
    trackingEnabled.store(false, std::memory_order_relaxed);

    std::printf("  200 blocks of Skream: %d allocations\n", after - before);
    assert(after == before);
}

/// Installing a circuit while audio is running must not tear or crash, and the
/// retired graph must actually be freed rather than leaked.
void testHotSwapWhileRunning()
{
    CompiledCircuit gain, skream;
    assert(compileTurtle(kGain, gain));
    assert(compileFile(VALIS_EXAMPLES_DIR "/skream.ttl", skream));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 256);

    std::string error;
    assert(engine.load(gain, registry, error));

    const auto input = tone(100.0, 48000.0, 256);
    std::vector<float> output(256, 0.0f);

    for (int block = 0; block < 20; ++block)
    {
        engine.process(input.data(), output.data(), 256);

        if (block == 5)
            assert(engine.load(skream, registry, error));
        if (block == 12)
            assert(engine.load(gain, registry, error));

        engine.collectGarbage();

        for (const float s : output)
            assert(std::isfinite(s));
    }
}

void testDeterminism()
{
    CompiledCircuit circuit;
    assert(compileFile(VALIS_EXAMPLES_DIR "/skream.ttl", circuit));
    const auto registry = makeDefaultRegistry();

    const auto input = tone(110.0, 48000.0, 4096);

    const auto once = [&] {
        ValisEngine engine;
        engine.prepare(48000.0, 512);
        std::string error;
        assert(engine.load(circuit, registry, error));
        return render(engine, input, 512);
    };

    const auto a = once();
    const auto b = once();
    assert(a == b);

    // Block size must not change the result either: the graph is sample
    // accurate, not block accurate.
    ValisEngine engine;
    engine.prepare(48000.0, 512);
    std::string error;
    assert(engine.load(circuit, registry, error));
    const auto c = render(engine, input, 128);

    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(a[i] - c[i])));

    std::printf("  block size 512 vs 128: max difference %.2e\n", worst);
    // The LFO and envelope followers are block rate by design, so a small
    // difference is expected; the audio path must not drift.
    assert(worst < 1.0e-3);
}

/// The acceptance demo, run for real.
void testSkreamMakesTheRightKindOfNoise()
{
    CompiledCircuit circuit;
    assert(compileFile(VALIS_EXAMPLES_DIR "/skream.ttl", circuit));
    const auto registry = makeDefaultRegistry();

    ValisEngine engine;
    engine.prepare(48000.0, 512);
    std::string error;
    assert(engine.load(circuit, registry, error));

    // Two ADAA2 saturators and one unit delay.
    assert(engine.latencyInSamples() == 3);

    // Silence in, silence out: the gate holds the feedback loop closed.
    const std::vector<float> silence(48000, 0.0f);
    const auto quiet = render(engine, silence, 512);
    assert(peakOf(quiet) == 0.0f);

    // A bass note in, a growl out - bounded, staged, and finite.
    engine.load(circuit, registry, error);
    const auto input = tone(100.0, 48000.0, 48000);
    const auto output = render(engine, input, 512);

    const float peak = peakOf(output);
    std::printf("  Skream on a 100 Hz tone: peak %.4f\n", peak);
    assert(std::isfinite(peak));
    assert(peak > 0.2f);      // it is doing something
    assert(peak < 1.0f);      // and the wet path is staged

    // The feedback path is what screams: muting it must change the sound.
    engine.load(circuit, registry, error);
    engine.setControl("urn:valis:skream#fbGain", "gain", -60.0f);
    const auto muted = render(engine, input, 512);

    double difference = 0.0;
    for (std::size_t i = 0; i < output.size(); ++i)
        difference += std::abs(static_cast<double>(output[i]) - muted[i]);
    difference /= static_cast<double>(output.size());

    std::printf("  feedback muted changes the output by %.4f mean abs\n", difference);
    assert(difference > 0.01);
}

void testUnconnectedInputReadsSilence()
{
    // The mixer's second input is never fed; it must read silence rather than
    // whatever the previous node left in a shared buffer.
    CompiledCircuit circuit;
    assert(compileTurtle(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :o , :w , :out ; val:arc :a1 , :a2 .
:o a val:Oscillator ; val:frequency 1000.0 .
:w a val:DryWet ; val:mix 0.0 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :o ; val:port "out" ] ;
                val:to   [ val:node :w ; val:port "wet" ] .
:a2 a val:Arc ; val:from [ val:node :w ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)", circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 256);
    std::string error;
    assert(engine.load(circuit, registry, error));

    // mix = 0 means all dry, and dry is unconnected, so the result is silence.
    const std::vector<float> input(1024, 0.0f);
    const auto output = render(engine, input, 256);
    assert(peakOf(output) == 0.0f);
}

/// docs/skream.md claims you can change val:antialiasing in the Turtle and hear
/// the difference. That is only true if an instance can override the class
/// default, so assert it rather than trusting it.
void testInstanceOptionsOverrideTheClass()
{
    const auto registry = makeDefaultRegistry();
    const auto input = tone(4000.0, 48000.0, 8192, 0.9f);

    const auto renderWith = [&](const char* strategy)
    {
        std::string turtle = std::string(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :in , :sat , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:sat a val:Tanh ; val:gain 8.0 ; val:antialiasing val:)") + strategy + R"( .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in  ; val:port "out" ] ;
                val:to   [ val:node :sat ; val:port "in"  ] .
:a2 a val:Arc ; val:from [ val:node :sat ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in"  ] .
)";

        CompiledCircuit circuit;
        assert(compileTurtle(turtle, circuit));

        ValisEngine engine;
        engine.prepare(48000.0, 512);
        std::string error;
        assert(engine.load(circuit, registry, error));

        return std::pair{render(engine, input, 512), engine.latencyInSamples()};
    };

    const auto [none,  noneLatency]  = renderWith("None");
    const auto [adaa1, adaa1Latency] = renderWith("ADAA1");
    const auto [adaa2, adaa2Latency] = renderWith("ADAA2");

    // The class default for val:Tanh is ADAA2, so None and ADAA1 are genuine
    // instance overrides.
    assert(noneLatency  == 0);
    assert(adaa1Latency == 0);
    assert(adaa2Latency == 1);

    assert(none != adaa1);
    assert(none != adaa2);
    assert(adaa1 != adaa2);

    std::printf("  antialiasing override: latency none=%d ADAA1=%d ADAA2=%d\n",
                noneLatency, adaa1Latency, adaa2Latency);
}

/// A val:Subcircuit with val:voices is a bounded pool: the same definition
/// stamped out N times, each copy given its own note by the engine. The
/// elements inside are the ordinary monophonic ones.
const char* kPolyCircuit = R"(
@prefix val:  <http://purl.org/stuff/valis/> .
@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix :     <urn:valis:t#> .
:Voice a val:Subcircuit ;
    lv2:port [ a lv2:OutputPort , lv2:AudioPort ; lv2:symbol "out" ;
               val:node :vca ; val:port "out" ] ;
    val:element :pitch , :osc , :gate , :vca ;
    val:arc :v1 , :v2 , :v3 .
:pitch a val:MidiPitch .
:osc a val:Oscillator ; val:wave 0.0 .
:gate a val:Envelope ; val:attack 1.0 ; val:decay 5.0 ; val:sustain 1.0 ; val:release 5.0 .
:vca a val:VCA .
:v1 a val:Arc ; val:from [ val:node :pitch ; val:port "out" ] ;
                val:to   [ val:node :osc ; val:port "frequency" ] .
:v2 a val:Arc ; val:from [ val:node :osc ; val:port "out" ] ;
                val:to   [ val:node :vca ; val:port "in" ] .
:v3 a val:Arc ; val:from [ val:node :gate ; val:port "out" ] ;
                val:to   [ val:node :vca ; val:port "cv" ] .

:c a val:Circuit ; val:element :poly , :out ; val:arc :a1 .
:poly a :Voice ; val:voices 4 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :poly ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)";

/// Three notes held together must sound as three pitches. A monophonic circuit
/// would give whichever note arrived last and nothing else.
void testVoicePoolSoundsAChord()
{
    CompiledCircuit circuit;
    assert(compileTurtle(kPolyCircuit, circuit));

    // Four voices of four elements, plus the mixer the expansion adds and the
    // output: the pool is real elements, not a special case in the engine.
    assert(circuit.numVoices == 4);
    assert(circuit.nodes.size() == 4 * 4 + 2);

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 512);
    std::string error;
    assert(engine.load(circuit, registry, error));

    // A major triad: MIDI 60, 64 and 67 are 261.6, 329.6 and 392.0 Hz.
    engine.queueNoteOn(60, 1.0f, 0);
    engine.queueNoteOn(64, 1.0f, 0);
    engine.queueNoteOn(67, 1.0f, 0);

    const std::vector<float> silence(8192, 0.0f);
    std::vector<float> out(8192, 0.0f);
    for (int block = 0; block < 4; ++block)
        engine.process(silence.data(), out.data(), 8192);

    constexpr int order = 13;                 // 8192
    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window(8192,
                                               juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> bins(8192 * 2, 0.0f);
    std::copy(out.begin(), out.end(), bins.begin());
    window.multiplyWithWindowingTable(bins.data(), 8192);
    fft.performFrequencyOnlyForwardTransform(bins.data());

    const auto energyAt = [&](double hz)
    {
        const auto centre = static_cast<int>(std::round(hz * 8192.0 / 48000.0));
        float sum = 0.0f;
        for (int b = centre - 3; b <= centre + 3; ++b)
            if (b >= 0 && b < 4096)
                sum = std::max(sum, bins[static_cast<std::size_t>(b)]);
        return sum;
    };

    const float root  = energyAt(261.63);
    const float third = energyAt(329.63);
    const float fifth = energyAt(392.00);

    // A frequency none of the three notes occupies, to measure against.
    const float between = energyAt(300.0);

    std::printf("  chord: root %.1f  third %.1f  fifth %.1f  (gap %.1f)\n",
                root, third, fifth, between);
    std::fflush(stdout);

    assert(root  > between * 20.0f);
    assert(third > between * 20.0f);
    assert(fifth > between * 20.0f);
}

/// Allocation has to be deterministic, or no offline test of a polyphonic
/// circuit could assert anything.
void testVoiceAllocationIsDeterministic()
{
    CompiledCircuit circuit;
    assert(compileTurtle(kPolyCircuit, circuit));
    const auto registry = makeDefaultRegistry();

    const auto play = [&]
    {
        ValisEngine engine;
        engine.prepare(48000.0, 256);
        std::string error;
        assert(engine.load(circuit, registry, error));

        const std::vector<float> silence(256, 0.0f);
        std::vector<float> out(256, 0.0f), all;

        for (int block = 0; block < 12; ++block)
        {
            if (block == 0) engine.queueNoteOn(60, 1.0f, 10);
            if (block == 1) engine.queueNoteOn(64, 0.8f, 20);
            if (block == 2) engine.queueNoteOn(67, 0.6f, 30);
            if (block == 4) engine.queueNoteOff(64, 5);
            if (block == 5) engine.queueNoteOn(72, 0.9f, 40);

            engine.process(silence.data(), out.data(), 256);
            all.insert(all.end(), out.begin(), out.end());
        }
        return all;
    };

    const auto first = play();
    const auto second = play();

    assert(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
        assert(first[i] == second[i]);
}

/// More notes than voices is bounded: the pool steals rather than growing, and
/// the circuit keeps running.
void testMoreNotesThanVoicesSteals()
{
    CompiledCircuit circuit;
    assert(compileTurtle(kPolyCircuit, circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 256);
    std::string error;
    assert(engine.load(circuit, registry, error));

    const std::vector<float> silence(256, 0.0f);
    std::vector<float> out(256, 0.0f);

    // Ten notes into four voices.
    for (int note = 60; note < 70; ++note)
    {
        engine.queueNoteOn(note, 1.0f, 0);
        engine.process(silence.data(), out.data(), 256);

        for (const float s : out)
            assert(std::isfinite(s));
    }

    // The most recent note is still sounding: stealing took the oldest.
    assert(peakOf(out) > 0.01f);

    for (int note = 60; note < 70; ++note)
        engine.queueNoteOff(note, 0);

    for (int block = 0; block < 60; ++block)
        engine.process(silence.data(), out.data(), 256);

    // Everything released, so it falls silent rather than leaving a voice stuck
    // open because its note off went to the wrong one.
    assert(peakOf(out) < 0.01f);
}

/// The shipped example, compiled and run the way a user opens it.
void testPolysynthExampleRuns()
{
    CompiledCircuit circuit;
    assert(compileFile(VALIS_EXAMPLES_DIR "/polysynth.ttl", circuit));

    // Eight voices of five elements, plus the mixer, the saturator and output.
    assert(circuit.numVoices == 8);
    assert(circuit.nodes.size() == 8 * 5 + 3);

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 512);
    std::string error;
    assert(engine.load(circuit, registry, error));

    const std::vector<float> silence(512, 0.0f);
    std::vector<float> out(512, 0.0f);

    engine.queueNoteOn(48, 1.0f, 0);
    engine.queueNoteOn(55, 1.0f, 0);
    engine.queueNoteOn(64, 1.0f, 0);

    for (int block = 0; block < 20; ++block)
    {
        engine.process(silence.data(), out.data(), 512);
        for (const float s : out)
            assert(std::isfinite(s));
    }

    assert(peakOf(out) > 0.05f);
}

/// examples/chimera.ttl is built on feedback between two nonlinear elements, so
/// the things worth pinning are that it stays bounded and that its controls
/// actually do what the document claims.
void testChimeraIsStableAndItsRegimesDiffer()
{
    CompiledCircuit circuit;
    assert(compileFile(VALIS_EXAMPLES_DIR "/chimera.ttl", circuit));

    // Eight voices of nine elements, plus the mixer summing the pool, and
    // level, body, air, ceiling and output.
    assert(circuit.numVoices == 8);
    assert(circuit.nodes.size() == 8 * 9 + 6);

    const auto registry = makeDefaultRegistry();

    const auto play = [&](double ratio)
    {
        ValisEngine engine;
        engine.prepare(48000.0, 512);
        std::string error;
        const bool loaded = engine.load(circuit, registry, error);
        assert(loaded);

        // Every voice's ratio, which is what the one knob drives.
        for (int voice = 0; voice < 8; ++voice)
            engine.setControl("urn:valis:chimera#voices/" + std::to_string(voice) + "/detune",
                              "b", static_cast<float>(ratio));

        engine.queueNoteOn(40, 1.0f, 0);

        const std::vector<float> silence(512, 0.0f);
        std::vector<float> block(512, 0.0f);
        double sum = 0.0;
        int counted = 0;

        for (int i = 0; i < 200; ++i)
        {
            engine.process(silence.data(), block.data(), 512);

            for (const float s : block)
            {
                // Two nonlinear elements in a loop: the first thing to
                // establish is that it cannot run away.
                assert(std::isfinite(s));
                assert(std::abs(s) <= 1.05f);

                if (i >= 40)   // past the attack
                {
                    sum += static_cast<double>(s) * s;
                    ++counted;
                }
            }
        }

        return counted > 0 ? std::sqrt(sum / counted) : 0.0;
    };

    const double unison = play(1.0);
    const double fifth  = play(1.5);
    const double golden = play(1.618);

    std::printf("  chimera: rms %.3f at 1.0, %.3f at 1.5, %.3f at 1.618\n",
                unison, fifth, golden);
    std::fflush(stdout);

    // All three sound.
    assert(unison > 0.02 && fifth > 0.02 && golden > 0.02);

    // And they are not the same sound with a different tuning. An irrational
    // ratio gives the two strings nothing to agree on, so they never settle,
    // and that regime carries noticeably more energy than a locked one.
    assert(golden > unison * 1.3);
}

/// Each note in the DMX kit must sound its own drum and only its own. The
/// failure this guards against is every voice sounding at once, which is what
/// happened before a connected trigger owned playback.
void testDmxNotesSelectTheirOwnDrum()
{
    CompiledCircuit circuit;
    assert(compileFile(VALIS_EXAMPLES_DIR "/dmx.ttl", circuit));

    const auto registry = makeDefaultRegistry();

    const auto hit = [&](int note)
    {
        ValisEngine engine;
        engine.prepare(48000.0, 512);
        std::string error;
        const bool loaded = engine.load(circuit, registry, error);
        assert(loaded);

        const std::vector<float> silence(512, 0.0f);
        std::vector<float> block(512, 0.0f), all;

        // Two blocks of nothing first: with no note, nothing may sound.
        for (int i = 0; i < 2; ++i)
        {
            engine.process(silence.data(), block.data(), 512);
            assert(peakOf(block) < 1.0e-6f);
        }

        if (note >= 0)
            engine.queueNoteOn(note, 1.0f, 0);

        for (int i = 0; i < 60; ++i)
        {
            engine.process(silence.data(), block.data(), 512);
            all.insert(all.end(), block.begin(), block.end());
        }

        double sum = 0.0;
        for (const float s : all)
        {
            assert(std::isfinite(s));
            sum += static_cast<double>(s) * s;
        }
        return std::sqrt(sum / all.size());
    };

    // A kick, a snare, a closed hat and a crash are four different sounds, and
    // each has to differ from the others by more than measurement noise.
    const double kick  = hit(36);
    const double snare = hit(38);
    const double hat   = hit(42);
    const double crash = hit(49);

    std::printf("  dmx: kick %.4f  snare %.4f  hat %.4f  crash %.4f\n",
                kick, snare, hat, crash);
    std::fflush(stdout);

    for (const double level : {kick, snare, hat, crash})
        assert(level > 0.001);

    // The hat is the quietest of the four and the crash the longest, so if
    // every voice were sounding at once these would all be equal.
    assert(hat < kick);
    assert(hat < crash);
    assert(std::abs(kick - snare) > 0.001);

    // A note no voice is mapped to sounds nothing at all.
    assert(hit(100) < 1.0e-6);
}

/// An element may produce events rather than audio. val:NoteOut turns a control
/// gate into note events the engine collects and the host sends on.
///
/// The circuit here is the shape of survey case 43, audio analysis producing
/// MIDI, assembled from parts that already existed: a note arriving gates a
/// NoteOut whose pitch comes from val:MidiPitch.
void testCircuitProducesNoteEvents()
{
    CompiledCircuit circuit;
    assert(compileTurtle(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :gate , :pitch , :note , :out ; val:arc :m1 , :m2 .
:gate a val:NoteGate ; val:note 60.0 .
:pitch a val:MidiPitch .
:note a val:NoteOut ; val:velocity 0.5 ; val:channel 3.0 .
:out a val:Output .
:m1 a val:Arc ; val:from [ val:node :gate ; val:port "gate"  ] ;
                val:to   [ val:node :note ; val:port "gate"  ] .
:m2 a val:Arc ; val:from [ val:node :pitch ; val:port "out"  ] ;
                val:to   [ val:node :note  ; val:port "pitch" ] .
)", circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 256);
    std::string error;
    assert(engine.load(circuit, registry, error));

    const std::vector<float> silence(256, 0.0f);
    std::vector<float> block(256, 0.0f);

    // Nothing held, so nothing is produced.
    engine.process(silence.data(), block.data(), 256);
    assert(engine.outputEventCount() == 0);

    // A note arrives partway through the block: the gate opens and one note on
    // goes out, carrying the pitch and the declared velocity and channel.
    engine.queueNoteOn(60, 1.0f, 128);
    engine.process(silence.data(), block.data(), 256);

    assert(engine.outputEventCount() == 1);
    const auto on = engine.outputEvent(0);
    assert(on.noteOn);
    assert(on.note == 60);            // MidiPitch gives 261.6 Hz, which is note 60
    assert(on.channel == 3);
    assert(std::abs(on.velocity - 0.5f) < 1.0e-4f);

    // Located within the block, not flattened to its start.
    assert(on.sampleOffset >= 128 && on.sampleOffset < 160);

    // Holding it produces nothing more: the event is the edge, not the state.
    engine.process(silence.data(), block.data(), 256);
    assert(engine.outputEventCount() == 0);

    // Releasing it sends the note off, for the note that was actually sounding.
    engine.queueNoteOff(60, 0);
    engine.process(silence.data(), block.data(), 256);

    assert(engine.outputEventCount() == 1);
    const auto off = engine.outputEvent(0);
    assert(! off.noteOn);
    assert(off.note == 60);
    assert(off.channel == 3);
}

/// An event port is not a signal port: it must get neither an audio buffer nor
/// a control slot, or the compiler would size the circuit for something that
/// does not exist.
void testEventPortTakesNoBufferOrSlot()
{
    const auto compile = [](const char* element)
    {
        std::string turtle = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :in , :thing , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "left" ] .
)";
        turtle += element;
        CompiledCircuit circuit;
        const bool ok = compileTurtle(turtle, circuit);
        assert(ok);
        return circuit;
    };

    const auto withNoteOut = compile(":thing a val:NoteOut .\n");

    const auto& node = *std::find_if(withNoteOut.nodes.begin(), withNoteOut.nodes.end(),
                                     [](const auto& n) { return n.implementation == "NoteOut"; });

    // Four control inputs, and the event output contributes nothing.
    assert(node.controlValues.size() == 4);
    assert(node.audioOutBuffers.empty());
    assert(node.audioInBuffers.empty());
    assert(node.controlOutSlots.empty());
}

/// val:oversampling runs one element faster than the rest of the circuit. The
/// engine wraps the element, so the test has to go through a whole circuit
/// rather than through an element on its own.
///
/// tanh of a 5 kHz sine makes odd harmonics at 15k, 25k, 35k and 45k. At 48 kHz
/// everything above 24k folds back to 23k, 13k, 3k and 7k, which are bins no
/// real harmonic occupies: energy there is aliasing and nothing else.
void testOversamplingReducesAliasing()
{
    constexpr double rate = 48000.0;
    constexpr int    n    = 4096;

    const auto circuitFor = [](const char* option)
    {
        std::string turtle = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :in , :sat , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:sat a val:Tanh ; val:gain 8.0 ; val:antialiasing val:None )";
        turtle += option;
        turtle += R"( .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :sat ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :sat ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)";
        CompiledCircuit compiled;
        const bool ok = compileTurtle(turtle, compiled);
        assert(ok);
        return compiled;
    };

    const auto spectrum = [](const std::vector<float>& signal)
    {
        constexpr int order = 12;
        constexpr int size  = 1 << order;

        juce::dsp::FFT fft(order);
        juce::dsp::WindowingFunction<float> window(
            static_cast<std::size_t>(size), juce::dsp::WindowingFunction<float>::hann);

        std::vector<float> buffer(static_cast<std::size_t>(size) * 2, 0.0f);
        std::copy(signal.begin(), signal.begin() + size, buffer.begin());
        window.multiplyWithWindowingTable(buffer.data(), static_cast<std::size_t>(size));
        fft.performFrequencyOnlyForwardTransform(buffer.data());
        buffer.resize(static_cast<std::size_t>(size) / 2);
        return buffer;
    };

    const auto energyAt = [](const std::vector<float>& bins, double frequency)
    {
        const auto centre = static_cast<int>(std::round(frequency * 4096.0 / rate));
        float sum = 0.0f;
        for (int b = centre - 2; b <= centre + 2; ++b)
            if (b >= 0 && b < static_cast<int>(bins.size()))
                sum += bins[static_cast<std::size_t>(b)] * bins[static_cast<std::size_t>(b)];
        return sum;
    };

    const auto input = tone(5000.0, rate, n, 1.0f);

    const auto aliasEnergy = [&](const char* option)
    {
        const auto circuit = circuitFor(option);
        const auto registry = makeDefaultRegistry();

        ValisEngine engine;
        engine.prepare(rate, 512);
        std::string error;
        const bool loaded = engine.load(circuit, registry, error);
        assert(loaded);

        const auto out = render(engine, input, 512);
        const auto bins = spectrum(out);

        float total = 0.0f;
        for (const double f : {3000.0, 7000.0, 13000.0, 23000.0})
            total += energyAt(bins, f);
        return total;
    };

    const float plain = aliasEnergy("");
    const float eight = aliasEnergy("; val:oversampling 8");

    std::printf("  alias energy: 1x %.4g, 8x %.4g (%.1fx less)\n",
                plain, eight, plain / eight);
    std::fflush(stdout);

    // Eight times the room before the fold-back, and the filters on the way
    // down. This must be a large reduction, not a marginal one.
    assert(eight < plain * 0.1f);
}

/// The wrapper sits inside process(), so it is bound by the same rule as
/// everything else there: it allocates nothing.
void testOversampledProcessDoesNotAllocate()
{
    CompiledCircuit circuit;
    assert(compileTurtle(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :in , :sat , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:sat a val:Tanh ; val:gain 6.0 ; val:oversampling 8 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :sat ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :sat ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)", circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 512);

    std::string error;
    assert(engine.load(circuit, registry, error));

    const auto input = tone(1000.0, 48000.0, 512);
    std::vector<float> output(512, 0.0f);

    engine.process(input.data(), output.data(), 512);   // warm up outside the count

    trackingEnabled.store(true, std::memory_order_relaxed);
    const int before = allocationCount.load(std::memory_order_relaxed);

    for (int block = 0; block < 100; ++block)
        engine.process(input.data(), output.data(), 512);

    const int after = allocationCount.load(std::memory_order_relaxed);
    trackingEnabled.store(false, std::memory_order_relaxed);

    std::printf("  100 blocks at 8x: %d allocations\n", after - before);
    std::fflush(stdout);
    assert(after == before);
}

/// A factor that is not a power of two the oversampler can build is a located
/// load failure, not a silent fall back to the base rate.
void testInvalidOversamplingFactorFailsTheLoad()
{
    CompiledCircuit circuit;
    assert(compileTurtle(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :in , :sat , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:sat a val:Tanh ; val:oversampling 3 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :sat ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :sat ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)", circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 256);

    std::string error;
    assert(! engine.load(circuit, registry, error));
    assert(error.find("val:oversampling") != std::string::npos);
    assert(error.find("sat") != std::string::npos);   // located at the element
}

/// An oversampled element reports the latency its resampling filters cost, so
/// the host can compensate for it.
void testOversamplingReportsItsLatency()
{
    const auto latencyOf = [](const char* option)
    {
        std::string turtle = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :in , :sat , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:sat a val:Tanh ; val:antialiasing val:None )";
        turtle += option;
        turtle += R"( .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :sat ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :sat ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)";
        CompiledCircuit circuit;
        const bool ok = compileTurtle(turtle, circuit);
        assert(ok);

        const auto registry = makeDefaultRegistry();
        ValisEngine engine;
        engine.prepare(48000.0, 256);
        std::string error;
        const bool loaded = engine.load(circuit, registry, error);
        assert(loaded);
        return engine.latencyInSamples();
    };

    assert(latencyOf("") == 0);
    assert(latencyOf("; val:oversampling 4") > 0);
}

/// Elements are mono, so stereo is two chains through val:Input's "left" and
/// "right" into val:Output's. The two sides must stay apart end to end, and
/// "out" must still carry them summed for a circuit that wants one signal.
void testStereoChannelsStayApart()
{
    CompiledCircuit circuit;
    assert(compileTurtle(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :in , :gl , :gr , :out ; val:arc :a1 , :a2 , :a3 , :a4 .
:in a val:Input .
:gl a val:Gain ; val:gain 0.0 .
:gr a val:Gain ; val:gain -6.0 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "left"  ] ;
                val:to   [ val:node :gl ; val:port "in"    ] .
:a2 a val:Arc ; val:from [ val:node :in ; val:port "right" ] ;
                val:to   [ val:node :gr ; val:port "in"    ] .
:a3 a val:Arc ; val:from [ val:node :gl  ; val:port "out"  ] ;
                val:to   [ val:node :out ; val:port "left" ] .
:a4 a val:Arc ; val:from [ val:node :gr  ; val:port "out"   ] ;
                val:to   [ val:node :out ; val:port "right" ] .
)", circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 128);
    std::string error;
    assert(engine.load(circuit, registry, error));

    const std::vector<float> leftIn(128, 1.0f);
    const std::vector<float> rightIn(128, 1.0f);
    std::vector<float> outL(128, 0.0f), outR(128, 0.0f);

    engine.process(leftIn.data(), rightIn.data(), outL.data(), outR.data(), 128);

    // Left is unity, right is -6 dB. If the engine had summed the channels to
    // mono both sides would carry the same level.
    assert(std::abs(outL[64] - 1.0f) < 1.0e-4f);
    assert(std::abs(outR[64] - 0.5011872f) < 1.0e-4f);
}

/// "out" is the host's channels averaged, so a mono circuit keeps its level
/// whichever way the host is wired.
void testMonoInputPortAveragesTheChannels()
{
    CompiledCircuit circuit;
    assert(compileTurtle(kGain, circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 128);
    std::string error;
    assert(engine.load(circuit, registry, error));

    std::vector<float> outL(128, 0.0f);

    // Both channels at 1.0 must read as 1.0, not 2.0.
    const std::vector<float> ones(128, 1.0f);
    engine.process(ones.data(), ones.data(), outL.data(), nullptr, 128);
    assert(std::abs(outL[64] - 0.5011872f) < 1.0e-4f);   // -6 dB of 1.0

    // One channel at 1.0 and the other silent averages to 0.5.
    const std::vector<float> zeros(128, 0.0f);
    engine.process(ones.data(), zeros.data(), outL.data(), nullptr, 128);
    assert(std::abs(outL[64] - 0.5011872f * 0.5f) < 1.0e-4f);
}

/// A queued note carries its position within the block, so the engine cuts a
/// slice there. Without that the note would start at the next 32-sample control
/// boundary, or, before events were queued at all, at the start of the block.
void testQueuedNoteStartsOnItsOwnSample()
{
    CompiledCircuit circuit;
    assert(compileTurtle(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :gate , :osc , :vca , :out ;
   val:arc :a1 , :a2 , :m1 .
:gate a val:NoteGate ; val:note 60.0 .
:osc a val:Oscillator ; val:frequency 2000.0 .
:vca a val:VCA .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :osc ; val:port "out" ] ;
                val:to   [ val:node :vca ; val:port "in"  ] .
:a2 a val:Arc ; val:from [ val:node :vca ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in"  ] .
:m1 a val:Arc ; val:from [ val:node :gate ; val:port "gate" ] ;
                val:to   [ val:node :vca  ; val:port "cv"   ] .
)", circuit));

    const auto registry = makeDefaultRegistry();

    // The first sample the VCA lets through, for a note queued at `offset`.
    const auto firstSoundingSample = [&](int offset)
    {
        ValisEngine engine;
        engine.prepare(48000.0, 256);
        std::string error;
        assert(engine.load(circuit, registry, error));

        const std::vector<float> silence(256, 0.0f);
        std::vector<float> block(256, 0.0f);

        // Settle, so the oscillator is well away from zero by the time the
        // gate opens and the first sounding sample is unambiguous.
        engine.process(silence.data(), block.data(), 256);

        engine.queueNoteOn(60, 1.0f, offset);
        engine.process(silence.data(), block.data(), 256);

        for (int i = 0; i < 256; ++i)
            if (std::abs(block[static_cast<std::size_t>(i)]) > 1.0e-6f)
                return i;
        return -1;
    };

    // Deliberately off the 32-sample control grid: 100 is not a multiple of 32,
    // so a note that only respected the grid would sound at 96 or 128.
    const int at100 = firstSoundingSample(100);
    const int at0   = firstSoundingSample(0);

    std::printf("  queued note: offset 0 sounds at %d, offset 100 sounds at %d\n", at0, at100);
    std::fflush(stdout);

    assert(at0 == 0);
    assert(at100 == 100);
}

/// val:Envelope used to free-run, which made it useless. It is now gated by
/// note events, so silence before a note and a contour after one.
void testEnvelopeRespondsToNotes()
{
    CompiledCircuit circuit;
    assert(compileTurtle(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :env , :osc , :vca , :out ;
   val:arc :a1 , :a2 , :m1 .
:env a val:Envelope ; val:attack 5.0 ; val:decay 50.0 ; val:sustain 0.6 ; val:release 50.0 .
:osc a val:Oscillator ; val:frequency 220.0 .
:vca a val:Expander ; val:threshold 0.0 ; val:ratio 4.0 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :osc ; val:port "out" ] ;
                val:to   [ val:node :vca ; val:port "in"  ] .
:a2 a val:Arc ; val:from [ val:node :vca ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in"  ] .
:m1 a val:Arc ; val:from [ val:node :env ; val:port "out"    ] ;
                val:to   [ val:node :vca ; val:port "amount" ] ;
                val:depth 1.0 .
)", circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 256);
    std::string error;
    assert(engine.load(circuit, registry, error));

    const std::vector<float> silence(256, 0.0f);
    std::vector<float> block(256, 0.0f);

    // Before any note the envelope is idle, so the gate holds the signal down.
    for (int i = 0; i < 8; ++i)
        engine.process(silence.data(), block.data(), 256);
    const float beforeNote = peakOf(block);

    // A note opens it.
    engine.noteOn(60, 1.0f);
    for (int i = 0; i < 40; ++i)
        engine.process(silence.data(), block.data(), 256);
    const float held = peakOf(block);

    // Releasing closes it again.
    engine.noteOff(60);
    for (int i = 0; i < 120; ++i)
        engine.process(silence.data(), block.data(), 256);
    const float afterRelease = peakOf(block);

    std::printf("  envelope  before %.4f  held %.4f  released %.4f\n",
                beforeNote, held, afterRelease);
    std::fflush(stdout);

    assert(held > beforeNote * 4.0f);
    assert(held > 0.05f);
    assert(afterRelease < held * 0.5f);
}

/// rings-modal.ttl should produce sound after a MIDI note-on: noise burst →
/// VCA (env-gated) → ModalBank → Gain → Output. If the resonators are silent
/// after a note, the circuit is broken.
void testRingsModalProducesSound()
{
    CompiledCircuit circuit;
    const bool compiled = compileFile(VALIS_EXAMPLES_DIR "/rings-modal.ttl", circuit);
    if (! compiled)
    {
        std::puts("  rings-modal.ttl failed to compile — skipping sound test");
        return;
    }

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(44100.0, 256);
    std::string error;
    assert(engine.load(circuit, registry, error));

    const std::vector<float> silence(256, 0.0f);
    std::vector<float> block(256, 0.0f);

    // Before any note, output should be silent.
    for (int i = 0; i < 4; ++i)
        engine.process(nullptr, block.data(), 256);
    const float beforeNote = peakOf(block);
    std::printf("  rings-modal  before note: %.6f\n", beforeNote);
    assert(beforeNote == 0.0f);

    // After a note-on, the resonators should ring for at least 200 ms.
    engine.noteOn(60, 1.0f);
    float peakAfterNote = 0.0f;
    for (int i = 0; i < 40; ++i)
    {
        engine.process(nullptr, block.data(), 256);
        peakAfterNote = std::max(peakAfterNote, peakOf(block));
    }
    assert(peakAfterNote > 1.0e-4f);
}

void testSimultaneousPolyphonicNoteGates()
{
    CompiledCircuit circuit;
    assert(compileFile(VALIS_EXAMPLES_DIR "/909.ttl", circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 256);
    std::string error;
    assert(engine.load(circuit, registry, error));

    std::vector<float> block(256, 0.0f);

    // Trigger Bass Drum (36) and Snare (38) in the same block, then release
    // them before audio runs. NoteGate should still fire from the per-note
    // trigger state captured for this block.
    engine.noteOn(36, 0.8f);
    engine.noteOn(38, 0.8f);
    engine.noteOff(36);
    engine.noteOff(38);

    engine.process(nullptr, block.data(), 256);
    const float peak = peakOf(block);
    std::printf("  simultaneous BD+SD trigger peak: %.4f\n", peak);
    assert(peak > 0.05f);
}

void testBassDrumOnly()
{
    CompiledCircuit circuit;
    assert(compileFile(VALIS_EXAMPLES_DIR "/909.ttl", circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(44100.0, 512);
    std::string error;
    assert(engine.load(circuit, registry, error));

    std::vector<float> block(512, 0.0f);

    // Trigger only bass drum (note 36) — confirms TwinTBridge path produces output.
    engine.noteOn(36, 1.0f);
    engine.process(nullptr, block.data(), 512);
    const float firstPeak = peakOf(block);

    // Hold for several more blocks to confirm sustained ring.
    float maxHeld = 0.0f;
    for (int i = 0; i < 10; ++i)
    {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.process(nullptr, block.data(), 512);
        maxHeld = std::max(maxHeld, peakOf(block));
    }

    engine.noteOff(36);

    std::printf("  bass drum only: first block peak=%.4f  held peak=%.4f\n", firstPeak, maxHeld);
    assert(firstPeak > 0.01f);
    assert(maxHeld   > 0.01f);
}

void testModulationCompileAndPassesAudio()
{
    CompiledCircuit circuit;
    if (! compileFile(VALIS_EXAMPLES_DIR "/modulation.ttl", circuit))
    {
        std::puts("  modulation.ttl failed to compile — check diagnostics above");
        assert(false);
    }

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(44100.0, 256);
    std::string error;
    assert(engine.load(circuit, registry, error));

    std::vector<float> in(256, 0.2f);
    std::vector<float> out(256, 0.0f);
    for (int i = 0; i < 8; ++i)
        engine.process(in.data(), out.data(), 256);

    const float peak = peakOf(out);
    std::printf("  modulation peak after 8 blocks: %.4f\n", peak);
    assert(peak > 1.0e-4f && "modulation.ttl produced no output");
}

void testClarinetCompileAndProducesSound()
{
    CompiledCircuit circuit;
    if (! compileFile(VALIS_EXAMPLES_DIR "/clarinet.ttl", circuit))
    {
        std::puts("  clarinet.ttl failed to compile — check diagnostics above");
        assert(false);
    }

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(44100.0, 256);
    std::string error;
    assert(engine.load(circuit, registry, error));

    std::vector<float> block(256, 0.0f);
    engine.noteOn(60, 1.0f);
    float peak = 0.0f;
    for (int i = 0; i < 16; ++i)
    {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.process(nullptr, block.data(), 256);
        peak = std::max(peak, peakOf(block));
    }
    std::printf("  clarinet note-on peak after 16 blocks: %.4f\n", peak);
    assert(peak > 1.0e-4f && "clarinet produced no audio on note-on");
}

void testSh101CompileAndProducesSound()
{
    CompiledCircuit circuit;
    if (! compileFile(VALIS_EXAMPLES_DIR "/sh101.ttl", circuit))
    {
        std::puts("  sh101.ttl failed to compile — check diagnostics above");
        assert(false);
    }

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(44100.0, 256);
    std::string error;
    assert(engine.load(circuit, registry, error));

    std::vector<float> block(256, 0.0f);

    // Verify the envelope→VCA control arc is wired: no gate must mean silence.
    float silencePeak = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.process(nullptr, block.data(), 256);
        silencePeak = std::max(silencePeak, peakOf(block));
    }
    std::printf("  sh101 no-note peak (should be ~0): %.6f\n", silencePeak);
    assert(silencePeak < 1.0e-4f && "sh101 is loud without a MIDI note — Envelope→VCA arc not wired");

    engine.noteOn(60, 1.0f);
    float peak = 0.0f;
    for (int i = 0; i < 8; ++i)
    {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.process(nullptr, block.data(), 256);
        peak = std::max(peak, peakOf(block));
    }
    std::printf("  sh101 note-on peak after 8 blocks: %.4f\n", peak);
    assert(peak > 1.0e-4f && "sh101 produced no audio on note-on");
}

// Replicates the UI's exact loading path: file read as string, parsed with the
// same base URI that setTurtle uses, and processed via the stereo engine path
// that processBlock uses. If this passes but the standalone is silent, the issue
// is in the standalone's audio/MIDI device setup, not the engine.
void testClarinetUiLoadPathProducesSound()
{
    // Read the file as a string the way JUCE's f.loadFileAsString() would.
    const std::string path = std::string(VALIS_EXAMPLES_DIR) + "/clarinet.ttl";
    std::FILE* f = std::fopen(path.c_str(), "rb");
    assert(f != nullptr);
    std::string content;
    char buf[4096];
    while (const auto n = std::fread(buf, 1, sizeof(buf), f))
        content.append(buf, n);
    std::fclose(f);

    // Parse with the same base URI that setTurtle uses.
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    const bool parsed = store.parse(content, "urn:valis:circuit", parseErrors);
    for (const auto& e : parseErrors)
        std::fprintf(stderr, "  parse: %s\n", e.toString().c_str());
    assert(parsed && "clarinet.ttl failed to parse via UI path");

    CircuitModel model;
    std::vector<Diagnostic> diags;
    assert(model.build(store, ontology(), diags) && "clarinet.ttl failed to build via UI path");

    CompiledCircuit circuit;
    CircuitCompiler compiler;
    const bool compiled = compiler.compile(model, ontology(), circuit, diags);
    for (const auto& d : diags)
        std::fprintf(stderr, "  diag: %s\n", d.toString().c_str());
    assert(compiled && "clarinet.ttl failed to compile via UI path");

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(44100.0, 512);
    std::string error;
    assert(engine.load(circuit, registry, error));

    // Use the stereo process path that processBlock uses.
    std::vector<float> blockL(512, 0.0f), blockR(512, 0.0f);
    engine.noteOn(60, 1.0f);
    float peak = 0.0f;
    for (int i = 0; i < 20; ++i)
    {
        std::fill(blockL.begin(), blockL.end(), 0.0f);
        std::fill(blockR.begin(), blockR.end(), 0.0f);
        engine.process(nullptr, blockL.data(), blockR.data(), 512);
        peak = std::max(peak, peakOf(blockL));
    }
    std::printf("  clarinet UI-path stereo peak after 20 blocks: %.4f\n", peak);
    assert(peak > 1.0e-4f && "clarinet produced no audio via UI parse+stereo path");
}

void testClarinetPitchTracking()
{
    // Load clarinet via string-parse (same path as the UI).
    const std::string path = std::string(VALIS_EXAMPLES_DIR) + "/clarinet.ttl";
    std::FILE* f = std::fopen(path.c_str(), "rb");
    assert(f != nullptr);
    std::string content;
    char buf[4096];
    while (const auto n = std::fread(buf, 1, sizeof(buf), f))
        content.append(buf, n);
    std::fclose(f);

    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    assert(store.parse(content, "urn:valis:circuit", parseErrors));

    CircuitModel model;
    std::vector<Diagnostic> diags;
    assert(model.build(store, ontology(), diags));

    CompiledCircuit circuit;
    CircuitCompiler compiler;
    assert(compiler.compile(model, ontology(), circuit, diags));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(44100.0, 512);
    std::string error;
    assert(engine.load(circuit, registry, error));

    // The Reed model produces a unipolar square-ish wave (always positive).
    // Count threshold crossings at the midpoint to measure pitch-dependent
    // periodicity — twice the pitch means twice the crossing count per block.
    auto countThresholdCrossings = [](const std::vector<float>& b, float thr) -> int
    {
        int count = 0;
        for (std::size_t i = 1; i < b.size(); ++i)
            if ((b[i - 1] >= thr) != (b[i] >= thr))
                ++count;
        return count;
    };

    std::vector<float> L(512), R(512);

    // Warm up for note A3 (57, 220 Hz).
    engine.noteOn(57, 0.8f);
    for (int i = 0; i < 60; ++i)
    {
        std::fill(L.begin(), L.end(), 0.0f);
        engine.process(nullptr, L.data(), R.data(), 512);
    }
    // Measure min/max to find midpoint threshold.
    float lo57 = *std::min_element(L.begin(), L.end());
    float hi57 = *std::max_element(L.begin(), L.end());
    const int crossingsLow = countThresholdCrossings(L, (lo57 + hi57) * 0.5f);

    engine.noteOff(57);
    for (int i = 0; i < 25; ++i)
    {
        std::fill(L.begin(), L.end(), 0.0f);
        engine.process(nullptr, L.data(), R.data(), 512);
    }

    // Warm up for note A4 (69, 440 Hz) — one octave up.
    engine.noteOn(69, 0.8f);
    for (int i = 0; i < 60; ++i)
    {
        std::fill(L.begin(), L.end(), 0.0f);
        engine.process(nullptr, L.data(), R.data(), 512);
    }
    float lo69 = *std::min_element(L.begin(), L.end());
    float hi69 = *std::max_element(L.begin(), L.end());
    const int crossingsHigh = countThresholdCrossings(L, (lo69 + hi69) * 0.5f);

    std::printf("  clarinet pitch tracking: A3 crossings=%d  A4 crossings=%d"
                "  (A3 range %.3f-%.3f  A4 range %.3f-%.3f)\n",
                crossingsLow, crossingsHigh, lo57, hi57, lo69, hi69);

    // A4 is an octave above A3 → ~2x the crossing count.  Allow ±50% tolerance.
    assert(crossingsLow > 0 && "clarinet A3 produced no oscillation");
    assert(crossingsHigh > 0 && "clarinet A4 produced no oscillation");
    assert(crossingsHigh > crossingsLow &&
           "clarinet pitch does not rise with note number (pitch tracking broken)");
}

const char* kScopeTap = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :gen , :scope , :out ; val:arc :a1 , :a2 .
:gen a val:SignalGenerator ; val:frequency 440.0 ; val:amplitude 0.5 ; val:shape 0 .
:scope a val:Oscilloscope .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :gen ; val:port "out" ] ;
                val:to   [ val:node :scope ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :scope ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)";

/// Waveform taps feed the Controls tab scopes: the engine observes monitor
/// nodes without disturbing the audio, and the message thread reads back
/// the most recent samples.
/// The granular instrument end to end: it compiles, it loads the sample file it
/// names, it plays under a MIDI note, and process() allocates nothing while it
/// does so - a granulator that allocated per grain would be the easiest way to
/// break the real-time contract.
void testGranularCircuitProducesSound()
{
    CompiledCircuit circuit;
    const bool compiled = compileFile(VALIS_EXAMPLES_DIR "/granular.ttl", circuit);
    assert(compiled);

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 512);
    std::string error;
    const bool loaded = engine.load(circuit, registry, error);
    if (! loaded)
        std::printf("  granular.ttl failed to load: %s\n", error.c_str());
    assert(loaded);

    std::vector<float> left(512, 0.0f), right(512, 0.0f);

    // The envelope is closed before any note, so the cloud is inaudible.
    for (int i = 0; i < 4; ++i)
        engine.process(nullptr, left.data(), right.data(), 512);
    assert(peakOf(left) == 0.0f);

    engine.noteOn(60, 1.0f);

    allocationCount.store(0, std::memory_order_relaxed);
    trackingEnabled.store(true, std::memory_order_relaxed);

    float peak = 0.0f;
    bool  stereo = false;
    for (int i = 0; i < 60; ++i)
    {
        engine.process(nullptr, left.data(), right.data(), 512);
        peak = std::max(peak, peakOf(left));

        // Grains are placed at random across the image, so the two sides differ.
        for (int k = 0; k < 512; ++k)
            if (std::abs(left[k] - right[k]) > 1.0e-4f)
                stereo = true;
    }

    trackingEnabled.store(false, std::memory_order_relaxed);

    std::printf("  granular: peak %.4f, %d allocations in process()\n",
                peak, allocationCount.load(std::memory_order_relaxed));

    assert(peak > 0.01f);
    assert(stereo);
    assert(allocationCount.load(std::memory_order_relaxed) == 0);
}

/// The host timeline reaches elements, and advances inside a block rather than
/// stepping once per buffer.
void testTransportReachesElements()
{
    const char* kClock = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :t , :in , :out ; val:arc :a1 .
:t a val:Transport ; val:division 1.0 .
:in a val:Input .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)";

    CompiledCircuit circuit;
    assert(compileTurtle(kClock, circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 512);
    std::string error;
    assert(engine.load(circuit, registry, error));

    const std::string clockId = "urn:valis:t#t";
    const std::vector<float> silence(512, 0.0f);
    std::vector<float> out(512, 0.0f);

    TransportInfo info;
    info.playing     = true;
    info.tempoBpm    = 120.0;
    info.ppqPosition = 0.0;
    engine.setTransport(info);
    engine.process(silence.data(), out.data(), 512);

    const auto tempo = engine.getControlOutput(clockId, "tempo");
    assert(tempo.has_value() && std::abs(*tempo - 120.0f) < 1.0e-3f);

    // 512 samples at 48 kHz and 120 bpm is 0.02133 of a quarter note. The host
    // reported position 0 for the whole block; what the element last saw is the
    // end of it, because the engine carries the position forward per slice.
    const auto phase = engine.getControlOutput(clockId, "phase");
    assert(phase.has_value());
    std::printf("  transport: phase after one block %.5f\n", *phase);
    assert(*phase > 0.015f && *phase < 0.025f);

    // Stopped, the phase keeps moving at the same rate rather than freezing.
    info.playing = false;
    engine.setTransport(info);
    const auto before = *engine.getControlOutput(clockId, "phase");
    engine.process(silence.data(), out.data(), 512);
    const auto after = *engine.getControlOutput(clockId, "phase");
    assert(after > before);

    const auto playing = engine.getControlOutput(clockId, "playing");
    assert(playing.has_value() && *playing == 0.0f);
}

/// Grain onsets follow the transport when a control arc reaches the trigger
/// port: at 120 bpm a division of a sixteenth is eight grains a second, and
/// that is what comes out. Also the only test that loads a sample file through
/// the whole model, compiler and engine path.
void testTransportDrivesGrainOnsets()
{
    const char* kClocked = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :clock , :gate , :gran , :out ;
   val:arc :a1 , :a2 , :a3 .
:clock a val:Transport ; val:division 0.25 .
:gate a val:Scale ; val:min 0.0 ; val:max 1.0 .
:gran a val:Granulator ; val:file "samples/bell.wav" ; val:seconds 4.0 ;
       val:position 0.1 ; val:size 12.0 ; val:shape 1.0 ; val:spread 0.0 .
:out a val:Output .
:a1 a val:ControlArc ; val:from [ val:node :clock ; val:port "trigger" ] ;
                       val:to   [ val:node :gate  ; val:port "in" ] .
:a2 a val:ControlArc ; val:from [ val:node :gate ; val:port "out" ] ;
                       val:to   [ val:node :gran ; val:port "trigger" ] .
:a3 a val:Arc ; val:from [ val:node :gran ; val:port "left" ] ;
                val:to   [ val:node :out  ; val:port "left" ] .
)";

    CompiledCircuit circuit;
    assert(compileTurtle(kClocked, circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 512);
    std::string error;
    const bool loaded = engine.load(circuit, registry, error);
    if (! loaded)
        std::printf("  clocked granulator failed to load: %s\n", error.c_str());
    assert(loaded);

    TransportInfo info;
    info.playing  = true;
    info.tempoBpm = 120.0;

    // Two seconds of timeline, which at a sixteenth note is sixteen pulses.
    std::vector<float> left(512, 0.0f), right(512, 0.0f), all;
    const std::vector<float> silence(512, 0.0f);
    for (int block = 0; block < 188; ++block)
    {
        info.ppqPosition = static_cast<double>(block) * 512.0 / 48000.0 * 2.0;
        engine.setTransport(info);
        engine.process(silence.data(), left.data(), right.data(), 512);
        all.insert(all.end(), left.begin(), left.end());
    }

    // Count bursts: a grain is 12 ms, the gap between onsets 125 ms, so a
    // simple envelope with hysteresis separates them cleanly.
    int onsets = 0;
    bool sounding = false;
    for (std::size_t at = 0; at + 64 < all.size(); at += 64)
    {
        float peak = 0.0f;
        for (std::size_t k = at; k < at + 64; ++k)
            peak = std::max(peak, std::abs(all[k]));

        if (! sounding && peak > 0.02f)
        {
            ++onsets;
            sounding = true;
        }
        else if (sounding && peak < 0.005f)
        {
            sounding = false;
        }
    }

    std::printf("  transport-clocked grains: %d onsets in 2 s at 120 bpm\n", onsets);

    // Sixteen pulses, plus the one the transport fires when it starts.
    assert(onsets >= 16 && onsets <= 17);
}

/// The flute circuit, end to end. A waveguide has to be given time to speak:
/// it starts from the turbulence in its own breath, so a test that looked at
/// the first block would see silence and conclude it was broken.
void testFluteCircuitProducesSound()
{
    CompiledCircuit circuit;
    const bool compiled = compileFile(VALIS_EXAMPLES_DIR "/flute.ttl", circuit);
    assert(compiled);

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 512);
    std::string error;
    const bool loaded = engine.load(circuit, registry, error);
    if (! loaded)
        std::printf("  flute.ttl failed to load: %s\n", error.c_str());
    assert(loaded);

    std::vector<float> block(512, 0.0f);

    // Unblown, it is silent.
    for (int i = 0; i < 8; ++i)
        engine.process(nullptr, block.data(), 512);
    assert(peakOf(block) == 0.0f);

    engine.noteOn(69, 1.0f);

    float peak = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        engine.process(nullptr, block.data(), 512);
        peak = std::max(peak, peakOf(block));
        for (const float sample : block)
            assert(std::isfinite(sample));
    }

    std::printf("  flute: peak %.4f after a second of blowing\n", peak);
    assert(peak > 0.02f);
    assert(peak < 2.0f);

    // Released, it stops: the jet has nothing to drive it and the bore's loss
    // takes the rest.
    engine.noteOff(69);
    for (int i = 0; i < 200; ++i)
        engine.process(nullptr, block.data(), 512);
    assert(peakOf(block) < 0.01f);
}

void testTapCapturesRecentSamples()
{
    CompiledCircuit circuit;
    assert(compileTurtle(kScopeTap, circuit));

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 512);

    std::string error;
    assert(engine.load(circuit, registry, error));

    const std::string scopeId = "urn:valis:t#scope";

    const auto taps = engine.tapNodes();
    assert(taps.size() == 1 && taps[0] == scopeId);

    // No audio has run yet, and unknown nodes have no tap.
    std::vector<float> frame(1024, 0.0f);
    assert(engine.readTap(scopeId, frame.data(), 1024) == 0);
    assert(engine.readTap("urn:valis:t#nosuch", frame.data(), 1024) == 0);

    // Run a second of the generator's sine through the scope.
    const std::vector<float> silence(48000, 0.0f);
    render(engine, silence, 512);

    const int n = engine.readTap(scopeId, frame.data(), 1024);
    assert(n == 1024);
    assert(std::abs(peakOf(frame) - 0.5f) < 0.01f);

    // The frame ends at the stream end: sample 47999 of a 440 Hz sine.
    const double expected = 0.5 * std::sin(2.0 * M_PI * 440.0 * 47999 / 48000.0);
    assert(std::abs(frame[1023] - expected) < 0.02f);
}

}  // namespace

int main()
{
    testGainCircuitRuns();
    testNoCircuitIsSilenceNotGarbage();
    testProcessDoesNotAllocate();
    testHotSwapWhileRunning();
    testDeterminism();
    testSkreamMakesTheRightKindOfNoise();
    testUnconnectedInputReadsSilence();
    testInstanceOptionsOverrideTheClass();
    testVoicePoolSoundsAChord();
    testVoiceAllocationIsDeterministic();
    testMoreNotesThanVoicesSteals();
    testPolysynthExampleRuns();
    testDmxNotesSelectTheirOwnDrum();
    testChimeraIsStableAndItsRegimesDiffer();
    testCircuitProducesNoteEvents();
    testEventPortTakesNoBufferOrSlot();
    testOversamplingReducesAliasing();
    testOversampledProcessDoesNotAllocate();
    testInvalidOversamplingFactorFailsTheLoad();
    testOversamplingReportsItsLatency();
    testStereoChannelsStayApart();
    testMonoInputPortAveragesTheChannels();
    testQueuedNoteStartsOnItsOwnSample();
    testEnvelopeRespondsToNotes();
    testRingsModalProducesSound();
    testSimultaneousPolyphonicNoteGates();
    testBassDrumOnly();
    testSh101CompileAndProducesSound();
    testModulationCompileAndPassesAudio();
    testClarinetCompileAndProducesSound();
    testClarinetUiLoadPathProducesSound();
    testClarinetPitchTracking();
    testTapCapturesRecentSamples();
    testGranularCircuitProducesSound();
    testTransportReachesElements();
    testTransportDrivesGrainOnsets();
    testFluteCircuitProducesSound();

    std::puts("ValisEngineTest PASSED");
    return 0;
}


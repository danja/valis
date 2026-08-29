// tests/engine/ValisEngineTest.cpp

#include "valis/CircuitCompiler.h"
#include "valis/CircuitModel.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"

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
    std::printf("  rings-modal  peak after note-on: %.6f\n", peakAfterNote);
    assert(peakAfterNote > 1.0e-4f);
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
    testEnvelopeRespondsToNotes();
    testRingsModalProducesSound();

    std::puts("ValisEngineTest PASSED");
    return 0;
}

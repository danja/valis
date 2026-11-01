// tests/engine/Tr909VelocityTest.cpp
//
// examples/909.ttl played softly. Every drum in the kit has to sound at any velocity: a drum that is
// silent below half velocity makes a quiet hi-hat part vanish with no message (MISTAKES.md). The toms
// also take their level from the velocity of their own note.

#include "valis/CircuitCompiler.h"
#include "valis/CircuitModel.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

/// The peak over half a second after a single hit of `note` at `velocity`, on a fresh engine.
float hit(int note, float velocity)
{
    CompiledCircuit circuit;
    const bool compiled = compileFile(VALIS_EXAMPLES_DIR "/909.ttl", circuit);
    assert(compiled);

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(48000.0, 256);
    std::string error;
    const bool loaded = engine.load(circuit, registry, error);
    assert(loaded);

    std::vector<float> block(256, 0.0f);
    float peak = 0.0f;
    engine.noteOn(note, velocity);
    for (int i = 0; i < 94; ++i)                      // about half a second
    {
        if (i == 6)
            engine.noteOff(note);                     // a short note, as a sequencer plays a drum
        std::fill(block.begin(), block.end(), 0.0f);
        engine.process(nullptr, block.data(), 256);
        for (const float s : block)
            peak = std::max(peak, std::abs(s));
    }
    return peak;
}

} // namespace

int main()
{
    struct Drum { int note; const char* name; };
    const Drum drums[] = { {36, "bass drum"}, {37, "rim shot"}, {38, "snare"}, {39, "clap"}, {41, "low tom"},
                           {42, "closed hat"}, {43, "mid tom"}, {46, "open hat"}, {50, "high tom"} };
    const float velocities[] = { 0.05f, 0.2f, 0.4f, 0.5f, 0.6f, 1.0f };

    for (const auto& drum : drums)
    {
        std::printf("  %-10s", drum.name);
        for (const float v : velocities)
        {
            const float peak = hit(drum.note, v);
            std::printf(" %.3f", peak);
            assert(peak > 1.0e-3f);                   // sounds at every velocity
        }
        std::printf("\n");
    }

    // Every drum is louder when hit harder, and a drum hit at velocity 1 plays at the level it always did.
    for (const auto& drum : drums)
    {
        const float loud = hit(drum.note, 1.0f);
        if (drum.note == 36)
        {
            // The bass drum is gated by its note's gate and held at one velocity too.
            assert(hit(drum.note, 0.3f) < 0.6f * loud);
            continue;
        }
        const float soft = hit(drum.note, 0.3f);
        std::printf("  %-10s soft/loud %.2f\n", drum.name, soft / loud);
        assert(soft < 0.6f * loud && soft > 0.15f * loud);
    }

    // The level is held while the drum rings after the note has ended: a snare hit at half velocity, 64 to 213 ms after a
    // note that lasted 32 ms, is about half as loud as one hit at full velocity, and not cut to nothing.
    {
        const auto ringing = [](float velocity)
        {
            CompiledCircuit circuit;
            const bool compiled = compileFile(VALIS_EXAMPLES_DIR "/909.ttl", circuit);
            assert(compiled);
            const auto registry = makeDefaultRegistry();
            ValisEngine engine;
            engine.prepare(48000.0, 256);
            std::string error;
            const bool loaded = engine.load(circuit, registry, error);
            assert(loaded);

            std::vector<float> block(256, 0.0f);
            double sum = 0.0;
            long count = 0;
            engine.noteOn(38, velocity);
            for (int i = 0; i < 40; ++i)
            {
                if (i == 6)
                    engine.noteOff(38);
                std::fill(block.begin(), block.end(), 0.0f);
                engine.process(nullptr, block.data(), 256);
                if (i >= 12)
                    for (const float v : block) { sum += static_cast<double>(v) * v; ++count; }
            }
            return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
        };
        const float half = ringing(0.5f), full = ringing(1.0f);
        std::printf("  snare ring after the note: half %.4f, full %.4f\n", half, full);
        assert(full > 1.0e-3f);
        assert(half > 0.35f * full && half < 0.65f * full);
    }

    // The closed hat chokes the open hat: an open hat left to ring is heard for a long time after it, and one
    // cut by a closed hat 50 ms in is not. The tail is measured from 200 to 450 ms, after the closed hat itself has died.
    {
        const auto tail = [](bool chokeIt)
        {
            CompiledCircuit circuit;
            const bool compiled = compileFile(VALIS_EXAMPLES_DIR "/909.ttl", circuit);
            assert(compiled);
            const auto registry = makeDefaultRegistry();
            ValisEngine engine;
            engine.prepare(48000.0, 256);
            std::string error;
            const bool loaded = engine.load(circuit, registry, error);
            assert(loaded);

            std::vector<float> block(256, 0.0f);
            double sum = 0.0;
            long count = 0;
            engine.noteOn(46, 1.0f);
            for (int i = 0; i < 100; ++i)                 // 533 ms
            {
                if (i == 9 && chokeIt)
                    engine.noteOn(42, 1.0f);              // 48 ms
                if (i == 11 && chokeIt)
                    engine.noteOff(42);
                std::fill(block.begin(), block.end(), 0.0f);
                engine.process(nullptr, block.data(), 256);
                if (i >= 38 && i < 84)                    // 200 to 450 ms
                    for (const float v : block) { sum += static_cast<double>(v) * v; ++count; }
            }
            return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
        };
        const float ringing = tail(false), choked = tail(true);
        std::printf("  open hat tail rms: ringing %.5f, choked %.5f\n", ringing, choked);
        assert(ringing > 1.0e-3f);
        assert(choked < 0.2f * ringing);
    }

    // And with no note, the kit is silent.
    {
        CompiledCircuit circuit;
        const bool compiled = compileFile(VALIS_EXAMPLES_DIR "/909.ttl", circuit);
        assert(compiled);
        const auto registry = makeDefaultRegistry();
        ValisEngine engine;
        engine.prepare(48000.0, 256);
        std::string error;
        const bool loaded = engine.load(circuit, registry, error);
        assert(loaded);
        std::vector<float> block(256, 0.0f);
        engine.process(nullptr, block.data(), 256);
        assert(*std::max_element(block.begin(), block.end()) == 0.0f);
    }

    std::puts("Tr909VelocityTest: ok");
    return 0;
}

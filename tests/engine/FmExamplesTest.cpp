// tests/engine/FmExamplesTest.cpp
//
// The three DX7-style examples, loaded and played through the engine as the
// plugin would. They are built from nested subcircuits stamped out as a voice
// pool, so this is also the test that nesting, polyphony and the FM elements
// work together.

#include "valis/CircuitCompiler.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct Rig
{
    Ontology ontology;
    ElementRegistry registry = makeDefaultRegistry();
    ValisEngine engine;
    CircuitModel model;

    explicit Rig(const std::string& name)
    {
        std::vector<std::string> errors;
        ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool loadedOntology = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(loadedOntology);

        std::ifstream file(std::string(VALIS_EXAMPLES_DIR) + "/" + name);
        std::ostringstream text;
        text << file.rdbuf();
        assert(! text.str().empty());

        std::vector<Diagnostic> diagnostics;
        rdf::TurtleStore store;
        std::vector<rdf::ParseError> parseErrors;
        const bool parsed = store.parse(text.str(), "urn:valis:circuit", parseErrors);
        assert(parsed);

        const bool built = model.build(store, ontology, diagnostics);
        for (const auto& d : diagnostics)
            std::printf("  %s: %s\n", name.c_str(), d.message.c_str());
        assert(built);

        CompiledCircuit compiled;
        CircuitCompiler compiler;
        const bool compiledOk = compiler.compile(model, ontology, compiled, diagnostics);
        assert(compiledOk);

        // Prepared before the circuit is installed, so its elements are built
        // for this rate.
        engine.prepare(kRate, kBlock);

        std::string error;
        const bool installed = engine.load(compiled, registry, error);
        if (! installed)
            std::printf("  %s: %s\n", name.c_str(), error.c_str());
        assert(installed);

    }

    /// Renders `seconds` and returns the mono output.
    std::vector<float> render(double seconds)
    {
        std::vector<float> all;
        std::vector<float> block(kBlock, 0.0f);
        for (int i = 0; i < static_cast<int>(seconds * kRate / kBlock); ++i)
        {
            engine.process(nullptr, block.data(), kBlock);
            all.insert(all.end(), block.begin(), block.end());
        }
        return all;
    }
};

float peak(const std::vector<float>& s, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    float p = 0.0f;
    for (std::size_t i = from; i < std::min(to, s.size()); ++i)
    {
        assert(std::isfinite(s[i]));
        p = std::max(p, std::abs(s[i]));
    }
    return p;
}

double amplitudeAt(const std::vector<float>& s, std::size_t from, std::size_t length, double hz)
{
    assert(from + length <= s.size());

    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < length; ++i)
    {
        const double phase = 6.283185307179586 * hz * static_cast<double>(i) / kRate;
        re += s[from + i] * std::cos(phase);
        im += s[from + i] * std::sin(phase);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(length);
}

const char* const kExamples[] = { "dx7-epiano.ttl", "dx7-bass.ttl", "dx7-bells.ttl", "dx7-brass.ttl" };

void testEachExamplePlaysAndStaysInRange()
{
    for (const char* name : kExamples)
    {
        Rig rig(name);

        assert(peak(rig.render(0.2)) == 0.0f);         // silent until played

        rig.engine.noteOn(57, 1.0f);
        const auto held = rig.render(1.0);
        assert(peak(held) > 0.05f);
        assert(peak(held) < 1.0f);

        // The patch is tuned to the note: A3 is 220 Hz, and it is what the
        // ear takes as the pitch of every one of these.
        assert(amplitudeAt(held, 9600, 14400, 220.0) > 0.01);
    }
}

void testVelocityIsLoudness()
{
    for (const char* name : kExamples)
    {
        Rig soft(name), hard(name);
        soft.engine.noteOn(57, 0.2f);
        hard.engine.noteOn(57, 1.0f);
        assert(peak(soft.render(0.5)) < peak(hard.render(0.5)));
    }
}

void testPolyphonyAndRelease()
{
    for (const char* name : kExamples)
    {
        Rig one(name), chord(name);
        one.engine.noteOn(57, 0.8f);
        chord.engine.noteOn(57, 0.8f);
        chord.engine.noteOn(64, 0.8f);
        chord.engine.noteOn(69, 0.8f);
        const auto single = one.render(0.5);
        const auto triad = chord.render(0.5);

        // The same first note is in both, so the extra voices only add.
        assert(amplitudeAt(triad, 8000, 12000, 220.0) > 0.0f);
        assert(amplitudeAt(triad, 8000, 12000, 329.63) > amplitudeAt(single, 8000, 12000, 329.63));

        // Released notes die away rather than sounding for ever.
        chord.engine.noteOff(57);
        chord.engine.noteOff(64);
        chord.engine.noteOff(69);
        const auto tail = chord.render(12.0);
        assert(peak(tail, tail.size() - 4800) < 0.001f);
    }
}

/// Every knob must land on elements that exist, on every voice, and turning
/// one down must change what is heard. The first knob of each patch is the
/// level of a modulator, so zero removes the sidebands it was making.
void testTheKnobsReachEveryVoice()
{
    for (const char* name : kExamples)
    {
        Rig normal(name), muted(name);
        const auto& params = muted.model.params();
        assert(params.size() >= 3);

        const auto& first = params.front();
        assert(first.propertySymbol.rfind("level", 0) == 0);
        assert(first.alsoTargets.size() == 7 || first.alsoTargets.size() == 5 || first.alsoTargets.size() == 3);   // one per voice

        muted.engine.setControl(first.targetNode, first.propertySymbol, 0.0f);
        for (const auto& [node, port] : first.alsoTargets)
            muted.engine.setControl(node, port, 0.0f);

        normal.engine.noteOn(57, 1.0f);
        muted.engine.noteOn(57, 1.0f);
        assert(normal.render(0.3) != muted.render(0.3));
    }
}

}  // namespace

int main()
{
    testEachExamplePlaysAndStaysInRange();
    testVelocityIsLoudness();
    testPolyphonyAndRelease();
    testTheKnobsReachEveryVoice();
    std::puts("FmExamplesTest passed");
    return 0;
}

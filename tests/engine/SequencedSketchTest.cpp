// tests/engine/SequencedSketchTest.cpp
//
// examples/blue-monday-sketch.ttl through the engine with the host rolling: four
// step sequencers fire drum samples and a fifth plays a bass. The elements have
// their own tests; this checks the circuit puts the hits where its patterns say,
// that a bar repeats exactly, and that it holds its level.

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

constexpr double kRate  = 48000.0;
constexpr double kTempo = 130.0;
constexpr int    kBlock = 512;

std::vector<float> render(double seconds)
{
    Ontology ontology;
    std::vector<std::string> errors;
    ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
    const bool loadedOntology = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
    assert(loadedOntology);

    std::ifstream stream(std::string(VALIS_EXAMPLES_DIR) + "/blue-monday-sketch.ttl");
    assert(stream.good());
    std::ostringstream text;
    text << stream.rdbuf();

    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    const bool parsed = store.parse(text.str(), "urn:valis:circuit", parseErrors);
    assert(parsed);

    CircuitModel model;
    std::vector<Diagnostic> diagnostics;
    const bool built = model.build(store, ontology, diagnostics);
    for (const auto& d : diagnostics) std::printf("  %s\n", d.message.c_str());
    assert(built);

    CompiledCircuit compiled;
    CircuitCompiler compiler;
    const bool compiledOk = compiler.compile(model, ontology, compiled, diagnostics);
    assert(compiledOk);

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(kRate, kBlock);
    std::string error;
    const bool installed = engine.load(compiled, registry, error);
    assert(installed);

    std::vector<float> out, in(kBlock, 0.0f), left(kBlock), right(kBlock);
    long position = 0;
    for (int b = 0; b < static_cast<int>(seconds * kRate / kBlock); ++b)
    {
        TransportInfo transport;
        transport.playing = true;
        transport.tempoBpm = kTempo;
        transport.ppqPosition = static_cast<double>(position) / kRate * kTempo / 60.0;
        engine.setTransport(transport);

        engine.process(in.data(), left.data(), right.data(), kBlock);
        out.insert(out.end(), left.begin(), left.end());
        position += kBlock;
    }
    return out;
}

/// RMS of one sixteenth, counting from the start of the transport.
double sixteenth(const std::vector<float>& s, int n)
{
    const double length = kRate * 60.0 / kTempo / 4.0;
    const auto from = static_cast<std::size_t>(n * length);
    const auto to   = std::min(static_cast<std::size_t>((n + 1) * length), s.size());
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        assert(std::isfinite(s[i]));
        sum += static_cast<double>(s[i]) * s[i];
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

void testDrumsLandOnTheirSteps()
{
    const auto out = render(8.0);

    // Kick on steps 0, 4, 8, 11 and 14 of the bar; the step after each has only the bass
    // and the tail of what came before. Compare bars two and three.
    for (const int bar : { 1, 2 })
    {
        const int base = bar * 16;
        for (const int step : { 0, 4, 8, 11, 14 })
            assert(sixteenth(out, base + step) > 1.5 * sixteenth(out, base + 1));
        std::printf("  bar %d: kick steps %.3f %.3f %.3f %.3f %.3f, quiet step %.3f\n", bar + 1,
                    sixteenth(out, base), sixteenth(out, base + 4), sixteenth(out, base + 8),
                    sixteenth(out, base + 11), sixteenth(out, base + 14), sixteenth(out, base + 1));
    }

    // The pattern repeats: the same step in consecutive bars sounds the same.
    for (int step = 0; step < 16; ++step)
    {
        const double a = sixteenth(out, 16 + step), b = sixteenth(out, 32 + step);
        assert(std::abs(a - b) < 0.02 + 0.1 * std::max(a, b));
    }

    float peak = 0.0f;
    for (const float v : out) peak = std::max(peak, std::abs(v));
    assert(peak < 1.0f);
    assert(peak > 0.2f);
}

}  // namespace

int main()
{
    testDrumsLandOnTheirSteps();
    std::printf("SequencedSketchTest PASSED\n");
    return 0;
}

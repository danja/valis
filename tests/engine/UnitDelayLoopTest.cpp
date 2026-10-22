// tests/engine/UnitDelayLoopTest.cpp
//
// What val:UnitDelay delays by. On its own it is one sample. Closing a loop it
// is longer, because elements run a whole block at a time and the delay's input
// is then the previous block's output of an element that runs after it. The docs
// state that, so a test holds them to it.

#include "valis/CircuitCompiler.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

/// Where an impulse at sample 0 comes out of `circuit`, as sample positions.
std::vector<int> echoes(const std::string& circuit, int blockSize)
{
    const std::string source =
        "@prefix val: <http://purl.org/stuff/valis/> .\n"
        "@prefix : <urn:valis:test#> .\n" + circuit;

    Ontology ontology;
    std::vector<std::string> errors;
    ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
    const bool loaded = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
    assert(loaded);

    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    const bool parsed = store.parse(source, "urn:valis:circuit", parseErrors);
    assert(parsed);

    std::vector<Diagnostic> diagnostics;
    CircuitModel model;
    const bool built = model.build(store, ontology, diagnostics);
    assert(built);
    CompiledCircuit compiled;
    CircuitCompiler compiler;
    const bool compiledOk = compiler.compile(model, ontology, compiled, diagnostics);
    for (const auto& d : diagnostics)
        std::printf("  %s\n", d.message.c_str());
    assert(compiledOk);

    ElementRegistry registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(kRate, blockSize);
    std::string error;
    const bool installed = engine.load(compiled, registry, error);
    assert(installed);

    std::vector<int> positions;
    std::vector<float> in(static_cast<std::size_t>(blockSize), 0.0f), out(in.size(), 0.0f);
    in[0] = 1.0f;
    for (int block = 0; block < 8; ++block)
    {
        engine.process(in.data(), out.data(), blockSize);
        in[0] = 0.0f;
        for (int i = 0; i < blockSize; ++i)
            if (out[static_cast<std::size_t>(i)] > 0.5f)
                positions.push_back(block * blockSize + i);
    }
    return positions;
}

}  // namespace

int main()
{
    const auto arc = [](const char* name, const char* from, const char* to)
    {
        return std::string(":") + name + " a val:Arc ; val:from [ val:node :" + from +
               " ; val:port \"out\" ] ; val:to [ val:node :" + to + " ; val:port \"in\" ] .\n";
    };

    // Feed-forward, the delay is one sample.
    const auto forward = echoes(
        ":c a val:Circuit ; val:element :in , :d , :out ; val:arc :a1 , :a2 .\n"
        ":in a val:Input . :d a val:UnitDelay . :out a val:Output .\n"
        + arc("a1", "in", "d") + arc("a2", "d", "out"), 512);
    assert(forward.size() == 1 && forward[0] == 1);

    // Round a loop it is the control slice plus one sample, whatever the host's
    // block size, and the engine's slice is 32 samples at most.
    for (const int block : {512, 64})
    {
        const auto loop = echoes(
            ":c a val:Circuit ; val:element :in , :mix , :d , :out ; val:arc :a1 , :a2 , :a3 , :a4 .\n"
            ":in a val:Input . :mix a val:Mixer . :d a val:UnitDelay . :out a val:Output .\n"
            + arc("a1", "in", "mix") + arc("a2", "mix", "out") + arc("a3", "mix", "d")
            + arc("a4", "d", "mix"), block);
        assert(loop.size() > 8);
        for (std::size_t i = 0; i < 8; ++i)
            assert(loop[i] == static_cast<int>(i) * 33);
    }

    std::puts("UnitDelayLoopTest passed");
    return 0;
}

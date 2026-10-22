// tests/engine/ExampleRig.h
//
// Loads a circuit from examples/ into an engine the way the plugin does, so a
// test can play it. Header only; every failure asserts with the diagnostics.

#pragma once

#include "valis/CircuitCompiler.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace valis::test {

struct ExampleRig
{
    static constexpr double kRate = 48000.0;
    static constexpr int kBlock = 512;

    Ontology ontology;
    ElementRegistry registry = makeDefaultRegistry();
    ValisEngine engine;
    CircuitModel model;

    explicit ExampleRig(const std::string& file)
    {
        std::vector<std::string> errors;
        ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool loadedOntology = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(loadedOntology);

        std::ifstream stream(std::string(VALIS_EXAMPLES_DIR) + "/" + file);
        std::ostringstream text;
        text << stream.rdbuf();
        assert(! text.str().empty());

        rdf::TurtleStore store;
        std::vector<rdf::ParseError> parseErrors;
        const bool parsed = store.parse(text.str(), "urn:valis:circuit", parseErrors);
        assert(parsed);

        std::vector<Diagnostic> diagnostics;
        const bool built = model.build(store, ontology, diagnostics);
        report(file, diagnostics);
        assert(built);

        CompiledCircuit compiled;
        CircuitCompiler compiler;
        const bool compiledOk = compiler.compile(model, ontology, compiled, diagnostics);
        report(file, diagnostics);
        assert(compiledOk);

        // Prepared before the circuit is installed, so its elements are built
        // for this rate.
        engine.prepare(kRate, kBlock);
        std::string error;
        const bool installed = engine.load(compiled, registry, error);
        if (! installed)
            std::printf("  %s: %s\n", file.c_str(), error.c_str());
        assert(installed);
    }

    /// Sets a Controls-panel parameter by its symbol, on every voice.
    void setParam(const std::string& symbol, float value)
    {
        for (const auto& binding : model.params())
        {
            if (binding.symbol != symbol)
                continue;

            engine.setControl(binding.targetNode, binding.propertySymbol, value);
            for (const auto& [node, port] : binding.alsoTargets)
                engine.setControl(node, port, value);
            return;
        }
        assert(false && "no such parameter");
    }

    /// Renders `seconds` of a circuit with no audio input, returning the left channel.
    std::vector<float> render(double seconds)
    {
        std::vector<float> all, left(kBlock), right(kBlock);
        for (int b = 0; b < static_cast<int>(seconds * kRate / kBlock); ++b)
        {
            engine.process(nullptr, left.data(), right.data(), kBlock);
            all.insert(all.end(), left.begin(), left.end());
        }
        return all;
    }

private:
    static void report(const std::string& file, const std::vector<Diagnostic>& diagnostics)
    {
        for (const auto& d : diagnostics)
            std::printf("  %s: %s\n", file.c_str(), d.message.c_str());
    }
};

}  // namespace valis::test

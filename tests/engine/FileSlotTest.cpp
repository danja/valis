// tests/engine/FileSlotTest.cpp
//
// The session's choice of file sits on top of the one the document declares: the
// processor replaces the file option on the compiled circuit before the engine loads it.
// This does the same to examples/timestretch.ttl and checks that the file played is the
// chosen one, which is what the Controls view's file slot relies on for val:TimeStretch.

#include "valis/CircuitCompiler.h"
#include "valis/DspElement.h"
#include "valis/FileOption.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi   = 3.14159265358979323846;

std::string writeTone(double hz)
{
    const auto path = (std::filesystem::temp_directory_path() / "valis_fileslot_tone.wav").string();
    std::ofstream file(path, std::ios::binary);
    const auto put32 = [&](std::uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](std::uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };

    const int n = static_cast<int>(3 * kRate);
    const auto bytes = static_cast<std::uint32_t>(n * 2);
    file.write("RIFF", 4); put32(36 + bytes); file.write("WAVE", 4);
    file.write("fmt ", 4); put32(16); put16(1); put16(1); put32(48000); put32(96000); put16(2); put16(16);
    file.write("data", 4); put32(bytes);
    for (int i = 0; i < n; ++i)
        put16(static_cast<std::uint16_t>(static_cast<std::int16_t>(
            std::lround(0.5 * std::sin(2.0 * kPi * hz * i / kRate) * 32767.0))));
    return path;
}

/// Zero crossings per second of the left channel after the first half second.
double crossingsPerSecond(const std::string& chosenFile)
{
    Ontology ontology;
    std::vector<std::string> errors;
    ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
    const bool loadedOntology = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
    assert(loadedOntology);

    std::ifstream stream(std::string(VALIS_EXAMPLES_DIR) + "/timestretch.ttl");
    std::ostringstream text;
    text << stream.rdbuf();

    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    const bool parsed = store.parse(text.str(), "urn:valis:circuit", parseErrors);
    assert(parsed);

    CircuitModel model;
    std::vector<Diagnostic> diagnostics;
    const bool built = model.build(store, ontology, diagnostics);
    assert(built);

    CompiledCircuit compiled;
    CircuitCompiler compiler;
    const bool compiledOk = compiler.compile(model, ontology, compiled, diagnostics);
    assert(compiledOk);

    // What ValisProcessor does with a session's choice.
    if (! chosenFile.empty())
    {
        bool replaced = false;
        for (auto& node : compiled.nodes)
        {
            if (! hasFileSlot(node.implementation))
                continue;

            const std::string key(fileOptionKey(node.implementation));
            auto option = std::find_if(node.options.begin(), node.options.end(),
                                       [&](const auto& o) { return o.first == key; });
            assert(option != node.options.end());
            option->second = chosenFile;
            replaced = true;
        }
        assert(replaced);
    }

    const auto registry = makeDefaultRegistry();
    ValisEngine engine;
    engine.prepare(kRate, 512);
    std::string error;
    const bool installed = engine.load(compiled, registry, error);
    if (! installed) std::printf("  %s\n", error.c_str());
    assert(installed);

    // Full speed, so the pitch is the file's.
    engine.setControl("player", "speed", 1.0f);

    std::vector<float> in(512, 0.0f), left(512), right(512), out;
    for (int b = 0; b < static_cast<int>(2.0 * kRate / 512); ++b)
    {
        engine.process(in.data(), left.data(), right.data(), 512);
        out.insert(out.end(), left.begin(), left.end());
    }

    int crossings = 0;
    for (std::size_t i = static_cast<std::size_t>(0.5 * kRate) + 1; i < out.size(); ++i)
        if (out[i - 1] <= 0.0f && out[i] > 0.0f)
            ++crossings;
    return crossings / (static_cast<double>(out.size()) / kRate - 0.5);
}

}  // namespace

int main()
{
    const double declared = crossingsPerSecond({});
    const double chosen   = crossingsPerSecond(writeTone(1000.0));

    std::printf("  declared file: %.0f crossings/s; chosen 1000 Hz tone: %.0f\n", declared, chosen);
    assert(std::abs(chosen - 1000.0) < 20.0);
    assert(std::abs(declared - 1000.0) > 100.0);   // the bell is not a 1 kHz sine

    std::puts("FileSlotTest PASSED");
    return 0;
}

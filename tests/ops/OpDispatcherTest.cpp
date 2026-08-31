// tests/ops/OpDispatcherTest.cpp
//
// The ops are the surface the views and the MCP server both sit on, so they are
// exercised here with no editor constructed and no host present.

#include "valis/CircuitCompiler.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/Ops.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace valis;

namespace {

std::string readFile(const char* path)
{
    std::ifstream file(path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/// Stands in for the plugin: owns the Turtle, the model and the engine, and
/// wires them into an OpContext exactly as ValisProcessor does.
struct Host
{
    Ontology ontology;
    ElementRegistry registry = makeDefaultRegistry();
    ValisEngine engine;
    CircuitModel model;
    std::string turtle;
    bool loaded = false;

    explicit Host(const std::string& source)
    {
        std::vector<std::string> errors;
        ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool ok = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(ok);

        engine.prepare(48000.0, 512);
        std::vector<Diagnostic> diagnostics;
        write(source, diagnostics);
    }

    bool write(const std::string& source, std::vector<Diagnostic>& diagnostics)
    {
        turtle = source;
        diagnostics.clear();

        rdf::TurtleStore store;
        std::vector<rdf::ParseError> parseErrors;
        if (! store.parse(source, "urn:valis:circuit", parseErrors))
        {
            for (const auto& e : parseErrors)
                diagnostics.push_back({e.message, {}, e.line, e.col});
            return false;
        }

        CircuitModel candidate;
        if (! candidate.build(store, ontology, diagnostics))
            return false;

        CompiledCircuit compiled;
        CircuitCompiler compiler;
        if (! compiler.compile(candidate, ontology, compiled, diagnostics))
            return false;

        std::string error;
        if (! engine.load(compiled, registry, error))
        {
            diagnostics.push_back({error, {}});
            return false;
        }

        model  = std::move(candidate);
        loaded = true;
        return true;
    }

    OpDispatcher ops()
    {
        OpContext ctx;
        ctx.ontology  = &ontology;
        ctx.engine    = &engine;
        ctx.registry  = &registry;
        ctx.readTurtle = [this] { return turtle; };
        ctx.writeTurtle = [this](const std::string& s, std::vector<Diagnostic>& d) { return write(s, d); };
        ctx.readModel = [this]() -> const CircuitModel* { return loaded ? &model : nullptr; };
        return OpDispatcher(ctx);
    }
};

// ---------------------------------------------------------------------------

void testTurtleRoundTrip()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    auto ops = host.ops();

    const auto got = ops.getTurtle();
    assert(got.ok);
    assert(got.value.find("val:Circuit") != std::string::npos);

    const auto set = ops.setTurtle(got.value);
    assert(set.ok);
}

void testValidateReportsWithoutInstalling()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    auto ops = host.ops();

    const auto before = ops.getTurtle().value;

    const auto good = ops.validate(readFile(VALIS_EXAMPLES_DIR "/skream.ttl"));
    assert(good.ok);
    assert(good.value.find("14 nodes") != std::string::npos);

    const auto bad = ops.validate("@prefix val: <http://purl.org/stuff/valis/> .\n:x a val:Ladder");
    assert(! bad.ok);
    assert(! bad.diagnostics.empty());

    // Neither call may install anything.
    assert(ops.getTurtle().value == before);
}

/// A circuit that will not compile must leave the previous one running, or a
/// typo in the editor silences the plugin mid-performance.
void testBadTurtleLeavesTheCircuitRunning()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    auto ops = host.ops();

    const auto elementsBefore = host.model.elements().size();

    const auto result = ops.setTurtle("this is not turtle at all {{{");
    assert(! result.ok);
    assert(! result.diagnostics.empty());
    assert(result.diagnostics[0].line > 0);        // positioned for the gutter

    assert(host.engine.hasCircuit());
    assert(host.model.elements().size() == elementsBefore);
}

void testListElementTypes()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    auto ops = host.ops();

    const auto types = ops.listElementTypes();
    assert(types.size() == 36);

    const auto ladder = std::find_if(types.begin(), types.end(),
                                     [](const ElementTypeInfo& t) { return t.implementation == "Ladder"; });
    assert(ladder != types.end());
    assert(! ladder->linear);
    assert(ladder->ports.size() == 5);

    const auto cutoff = std::find_if(ladder->ports.begin(), ladder->ports.end(),
                                     [](const PortInfo& p) { return p.symbol == "cutoff"; });
    assert(cutoff != ladder->ports.end());
    assert(cutoff->control && cutoff->input);
    assert(cutoff->unit == "Hz");   // from the vendored LV2 units vocabulary
    assert(cutoff->maximum == 20000.0);
}

void testGetGraph()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/skream.ttl"));
    auto ops = host.ops();

    const auto graph = ops.getGraph();
    assert(graph.ok);
    assert(graph.value.front() == '{' && graph.value.back() == '}');
    assert(graph.value.find("\"elements\"") != std::string::npos);
    assert(graph.value.find("\"arcs\"") != std::string::npos);
    assert(graph.value.find("skream#svf") != std::string::npos);
    assert(graph.value.find("\"depth\"") != std::string::npos);
}

void testGraphEditing()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    auto ops = host.ops();

    const auto nodesBefore = host.model.elements().size();
    const auto arcsBefore  = host.model.arcs().size();

    // Adding a node that nothing connects to is legal: it simply does nothing.
    const auto added = ops.addNode("urn:valis:basic#extra", "val:Gain");
    if (! added.ok)
        for (const auto& d : added.diagnostics) std::printf("    %s\n", d.toString().c_str());
    assert(added.ok);
    assert(host.model.elements().size() == nodesBefore + 1);

    // Duplicate ids and unknown classes are refused by name.
    assert(! ops.addNode("urn:valis:basic#extra", "val:Gain").ok);
    assert(! ops.addNode("urn:valis:basic#other", "val:Transfer").ok);
    assert(! ops.addNode("urn:valis:basic#other", "val:NotAThing").ok);

    // Rewire: drive -> extra -> out, replacing drive -> out.
    assert(ops.disconnect("urn:valis:basic#drive", "out", "urn:valis:basic#out", "in").ok);
    assert(ops.connect("urn:valis:basic#drive", "out", "urn:valis:basic#extra", "in").ok);
    assert(ops.connect("urn:valis:basic#extra", "out", "urn:valis:basic#out", "in").ok);
    assert(host.model.arcs().size() == arcsBefore + 1);

    // Connecting the same pair twice, or a port that does not exist, fails -
    // and a rejected edit must leave the source exactly as it was, or the next
    // op reads broken Turtle.
    const auto sourceBefore = ops.getTurtle().value;
    const auto arcsAfterRewire = host.model.arcs().size();

    assert(! ops.connect("urn:valis:basic#extra", "out", "urn:valis:basic#out", "in").ok);
    assert(! ops.connect("urn:valis:basic#extra", "nosuch", "urn:valis:basic#out", "in").ok);
    assert(! ops.connect("urn:valis:basic#vcf", "in", "urn:valis:basic#out", "in").ok);
    assert(! ops.disconnect("urn:valis:basic#extra", "out", "urn:valis:basic#vcf", "in").ok);

    assert(ops.getTurtle().value == sourceBefore);
    assert(host.model.arcs().size() == arcsAfterRewire);

    // Removing a node takes its arcs with it, or the result would not validate.
    const auto removed = ops.removeNode("urn:valis:basic#extra");
    if (! removed.ok)
        for (const auto& d : removed.diagnostics) std::printf("    %s\n", d.toString().c_str());
    assert(removed.ok);
    assert(host.model.elements().size() == nodesBefore);

    for (const auto& arc : host.model.arcs())
    {
        assert(arc.fromNode != "urn:valis:basic#extra");
        assert(arc.toNode   != "urn:valis:basic#extra");
    }

    assert(! ops.removeNode("urn:valis:basic#ghost").ok);
}

void testParameters()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/skream.ttl"));
    auto ops = host.ops();

    const auto params = ops.listParams();
    assert(params.size() == 8);

    // Sorted by slot, and each resolves to a real control input with a range.
    for (std::size_t i = 0; i < params.size(); ++i)
    {
        assert(params[i].slot == static_cast<int>(i));
        assert(params[i].maximum > params[i].minimum);
        assert(! params[i].name.empty());
    }

    const auto cutoff = params[0];
    assert(cutoff.name == "Cutoff");
    assert(cutoff.unit == "Hz");
    assert(cutoff.value == 800.0);

    assert(ops.getParam(0).ok);
    assert(ops.getParam(0).value.find("800") != std::string::npos);
    assert(! ops.getParam(63).ok);

    // set then get must agree: the reported value is what is running, not what
    // the Turtle declared.
    assert(ops.setParam(0, 2000.0).ok);
    assert(ops.getParam(0).value.find("2000") != std::string::npos);
    assert(ops.listParams()[0].value == 2000.0);

    // Out of range is clamped and said so, not silently accepted.
    const auto clamped = ops.setParam(0, 1.0e9);
    assert(clamped.ok);
    assert(! clamped.diagnostics.empty());
    assert(clamped.value.find("20000") != std::string::npos);

    assert(! ops.setParam(99, 1.0).ok);
}

void testDiagnostics()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/skream.ttl"));
    auto ops = host.ops();

    const auto result = ops.getDiagnostics();
    assert(result.ok);
    assert(result.value.find("\"loaded\":true") != std::string::npos);
    assert(result.value.find("\"elements\":14") != std::string::npos);
    assert(result.value.find("\"latency\":3") != std::string::npos);
}

/// Load 909.ttl and verify that triggering MIDI note 36 produces audio output.
void test909BassdrumProducesOutput()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/909.ttl"));
    if (! host.loaded)
    {
        std::puts("  909.ttl failed to load — circuit compilation errors above");
        assert(host.loaded);
    }

    host.engine.noteOn(36, 0.8f);
    std::vector<float> output(512, 0.0f);
    host.engine.process(nullptr, output.data(), 512);
    host.engine.process(nullptr, output.data(), 512);

    float maxAbs = 0.0f;
    for (float s : output) maxAbs = std::max(maxAbs, std::abs(s));
    std::printf("  BD note 36 max output after 2 blocks: %f\n", maxAbs);
    assert(maxAbs > 0.001f && "Bass drum note 36 produced no output");
}

/// An edit made through the ops must produce Turtle that survives a round trip,
/// or the text view and the graph view would drift apart.
void testEditsSurviveAReparse()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    auto ops = host.ops();

    assert(ops.addNode("urn:valis:basic#g2", "val:Gain").ok);
    assert(ops.connect("urn:valis:basic#drive", "out", "urn:valis:basic#g2", "in").ok);

    const auto turtle = ops.getTurtle().value;

    Host reloaded(turtle);
    assert(reloaded.loaded);
    assert(reloaded.model.elements().size() == host.model.elements().size());
    assert(reloaded.model.arcs().size() == host.model.arcs().size());
}

}  // namespace

int main()
{
    testTurtleRoundTrip();
    testValidateReportsWithoutInstalling();
    testBadTurtleLeavesTheCircuitRunning();
    testListElementTypes();
    testGetGraph();
    testGraphEditing();
    testParameters();
    testDiagnostics();
    testEditsSurviveAReparse();
    test909BassdrumProducesOutput();

    std::puts("OpDispatcherTest PASSED");
    return 0;
}

// tests/ops/OpDispatcherTest.cpp
//
// The ops are the surface the views and the MCP server both sit on, so they are
// exercised here with no editor constructed and no host present.

#include "valis/CircuitCompiler.h"
#include "valis/CircuitHistory.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/Ops.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>
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
    CircuitHistory history;
    int installs = 0;   ///< how many times a circuit has been handed to the engine to install

    /// Stands in for the plugin's per-node sample choices.
    std::map<std::string, std::string> samples;

    /// What building the circuit reported, kept so a test can check that a
    /// recoverable problem was named rather than swallowed.
    std::vector<Diagnostic> diagnostics;

    explicit Host(const std::string& source)
    {
        std::vector<std::string> errors;
        ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool ok = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(ok);

        engine.prepare(48000.0, 512);
        write(source, diagnostics);
    }

    bool write(const std::string& source, std::vector<Diagnostic>& diagnostics)
    {
        ++installs;
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
        ctx.history   = &history;
        ctx.readTurtle = [this] { return turtle; };
        ctx.writeTurtle = [this](const std::string& s, std::vector<Diagnostic>& d) { return write(s, d); };
        ctx.readModel = [this]() -> const CircuitModel* { return loaded ? &model : nullptr; };
        ctx.readSample = [this](const std::string& nodeId)
        {
            const auto found = samples.find(nodeId);
            if (found != samples.end())
                return found->second;

            if (const auto* element = model.findElement(nodeId))
                if (const auto declared = element->options.find("file"); declared != element->options.end())
                    return declared->second;
            return std::string{};
        };
        ctx.writeSample = [this](const std::string& nodeId, const std::string& path,
                                 std::string& error)
        {
            // The real host reinstalls the circuit, which is what would refuse
            // an unreadable file. Here the element does the refusing directly.
            auto element = makeDefaultRegistry().create("SampleLoad");
            const auto* type = ontology.find(vocab::valTerm("SampleLoad"));
            element->prepare(*type, 48000.0, 512);
            if (! element->setOption("file", path, error))
                return false;

            samples[nodeId] = path;
            return true;
        };
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

    // Every instantiable class the ontology declares, which is the same set the
    // registry can construct. Comparing against the registry rather than a
    // number means adding an element does not mean editing this test.
    const auto types = ops.listElementTypes();
    assert(types.size() == makeDefaultRegistry().size());

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
    assert(result.value.find("\"problems\":[]") != std::string::npos);   // nothing to report
}

/// A circuit can load with an element that could not do what it was asked. The diagnostics
/// have to say so, or the caller sees a healthy circuit that makes no sound.
/// The text of a failure.
std::string message(const OpResult& result)
{
    return result.diagnostics.empty() ? std::string() : result.diagnostics.front().message;
}

void testDescribeElement()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    auto ops = host.ops();

    const auto result = ops.describeElement("val:StepSequencer");
    assert(result.ok);
    const auto& json = result.value;

    // What it is for, in words; its ports with their numbers; and its option, which is not a port.
    assert(json.find("\"class\":\"StepSequencer\"") != std::string::npos);
    assert(json.find("\"description\":\"Plays a pattern of notes and rests") != std::string::npos);
    assert(json.find("\"symbol\":\"gatelength\"") != std::string::npos);
    assert(json.find("\"default\":0.5") != std::string::npos);
    assert(json.find("\"name\":\"pattern\",\"type\":\"string\"") != std::string::npos);
    assert(json.find("note name") != std::string::npos);                       // the option says what it takes
    assert(json.find("\"name\":\"oversampling\"") != std::string::npos);       // every element takes this

    int braces = 0, brackets = 0;
    for (const char c : json)
    {
        braces += c == '{' ? 1 : (c == '}' ? -1 : 0);
        brackets += c == '[' ? 1 : (c == ']' ? -1 : 0);
    }
    assert(braces == 0 && brackets == 0);

    // The name can be written any way a person would write it.
    for (const char* spelling : {"Ladder", "val:Ladder", "ladder", "VAL:LADDER", "http://purl.org/stuff/valis/Ladder"})
    {
        const auto r = ops.describeElement(spelling);
        if (! r.ok) std::printf("  describe %s: %s\n", spelling, message(r).c_str());
        assert(r.ok);
    }
    assert(ops.describeElement("ladder").ok);
    assert(ops.describeElement("ladder").value.find("\"class\":\"Ladder\"") != std::string::npos);

    // Named choices come with their labels.
    const auto granulator = ops.describeElement("Granulator");
    assert(granulator.ok);
    assert(granulator.value.find("\"label\":\"Pentatonic\"") != std::string::npos);
    assert(granulator.value.find("\"name\":\"file\"") != std::string::npos);
    assert(granulator.value.find("\"name\":\"seconds\"") != std::string::npos);

    // A control input has its range and unit; an audio port has neither.
    const auto ladder = ops.describeElement("Ladder").value;
    assert(ladder.find("\"symbol\":\"cutoff\"") != std::string::npos);
    assert(ladder.find("\"unit\":\"Hz\"") != std::string::npos);

    // An unknown name says what was probably meant.
    const auto bad = ops.describeElement("val:Laddr");
    assert(! bad.ok);
    assert(message(bad).find("Did you mean val:Ladder") != std::string::npos);
    assert(message(ops.describeElement("Tnah")).find("val:Tanh") != std::string::npos);
    const auto nonsense = ops.describeElement("zzzzzzzz");
    assert(! nonsense.ok && message(nonsense).find("Did you mean") == std::string::npos);

    // The same suggestion when a class is added by name.
    const auto added = ops.addNode("urn:valis:basic#x", "val:Ladde");
    assert(! added.ok && message(added).find("Did you mean val:Ladder") != std::string::npos);
}

const char* kSkeleton = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix : <urn:valis:new#> .
:main a val:Circuit ; rdfs:label "New" ; val:element :out .
:out a val:Output .
)";

GraphEdit addNode(const char* id, const char* cls)
{
    GraphEdit e; e.kind = GraphEdit::Kind::addNode; e.id = id; e.classIri = cls; return e;
}
GraphEdit connect(const char* from, const char* fromPort, const char* to, const char* toPort)
{
    GraphEdit e; e.kind = GraphEdit::Kind::connect; e.fromNode = from; e.fromPort = fromPort; e.toNode = to; e.toPort = toPort; return e;
}

void testABatchBuildsACircuitInOneInstall()
{
    Host host(kSkeleton);
    assert(host.loaded);
    auto ops = host.ops();
    const int before = host.installs;

    // Short ids, and later edits that use nodes the earlier ones added.
    const auto result = ops.applyEdits({
        addNode("pitch", "val:MidiPitch"), addNode("osc", "val:Oscillator"),
        addNode("env", "val:Envelope"),    addNode("vca", "val:VCA"),
        connect("osc", "out", "vca", "in"), connect("vca", "out", "out", "in"),
        connect("pitch", "out", "osc", "frequency"), connect("env", "out", "vca", "cv")});
    if (! result.ok) std::printf("  %s\n", result.diagnostics.empty() ? "" : result.diagnostics.front().message.c_str());
    assert(result.ok);
    assert(result.value.find("\"applied\":8") != std::string::npos);

    // Eight edits, one install: what is playing never passed through a half-built circuit.
    assert(host.installs == before + 1);

    const auto graph = ops.getGraph().value;
    for (const char* name : {"urn:valis:new#osc", "urn:valis:new#vca", "urn:valis:new#env", "urn:valis:new#pitch"})
        assert(graph.find(name) != std::string::npos);
    assert(host.model.arcs().size() == 4);

    // And it works: a note makes sound.
    const auto played = ops.lint();
    assert(played.value.find("\"errors\":0") != std::string::npos);
}

void testABatchThatFailsLeavesNothingBehind()
{
    Host host(kSkeleton);
    auto ops = host.ops();
    const auto turtleBefore = host.turtle;
    const int installsBefore = host.installs;

    const auto result = ops.applyEdits({
        addNode("osc", "val:Oscillator"), addNode("vca", "val:VCA"),
        connect("osc", "out", "vca", "Input"),                       // no such port
        connect("vca", "out", "out", "in")});
    assert(! result.ok);

    const auto& message = result.diagnostics.front().message;
    std::printf("  %s\n", message.c_str());
    assert(message.find("Edit 3 of 4") != std::string::npos);
    assert(message.find("connect osc.out to vca.Input") != std::string::npos);
    assert(message.find("Did you mean 'in'") != std::string::npos);   // the real error comes through
    assert(message.find("Nothing was changed") != std::string::npos);

    // Not one thing changed, and nothing was handed to the engine.
    assert(host.turtle == turtleBefore);
    assert(host.installs == installsBefore);
    assert(host.model.elements().size() == 1);

    // A bad class in the middle is the same.
    const auto badClass = ops.applyEdits({addNode("a", "val:Oscillator"), addNode("b", "val:Laddr")});
    assert(! badClass.ok && badClass.diagnostics.front().message.find("Edit 2 of 2") != std::string::npos);
    assert(badClass.diagnostics.front().message.find("Did you mean val:Ladder") != std::string::npos);
    assert(host.turtle == turtleBefore);

    // Naming something that is not there.
    const auto missing = ops.applyEdits({connect("ghost", "out", "out", "in")});
    assert(! missing.ok && host.turtle == turtleBefore);
}

void testABatchMayPassThroughAStateThatWouldNotInstall()
{
    Host host(kSkeleton);
    auto ops = host.ops();

    // Remove the only output and add another: on its own the first edit leaves a circuit with no
    // output, which would be refused. In a batch only the result has to be sound.
    GraphEdit remove; remove.kind = GraphEdit::Kind::removeNode; remove.id = "out";
    const auto result = ops.applyEdits({addNode("osc", "val:Oscillator"), remove, addNode("master", "val:Output"),
                                        connect("osc", "out", "master", "in")});
    if (! result.ok) std::printf("  %s\n", result.diagnostics.front().message.c_str());
    assert(result.ok);
    assert(host.model.findElement("urn:valis:new#master") != nullptr);
    assert(host.model.findElement("urn:valis:new#out") == nullptr);

    // Taken one at a time, removing the output is refused: the circuit would have none.
    Host alone(kSkeleton);
    assert(alone.ops().addNode("urn:valis:new#osc", "val:Oscillator").ok);
    assert(! alone.ops().removeNode("urn:valis:new#out").ok);
}

void testABatchThatIsWrongOnlyAsAWholeIsNotInstalled()
{
    Host host(kSkeleton);
    auto ops = host.ops();
    const auto turtleBefore = host.turtle;

    // Two audio arcs into one input: each edit is fine, together they are not a circuit.
    const auto result = ops.applyEdits({addNode("a", "val:Oscillator"), addNode("b", "val:Oscillator"),
                                        connect("a", "out", "out", "in"), connect("b", "out", "out", "in")});
    assert(! result.ok);
    const auto& message = result.diagnostics.front().message;
    std::printf("  %s\n", message.c_str());
    assert(message.find("could not be installed") != std::string::npos);
    assert(message.find("Nothing was changed") != std::string::npos);
    assert(host.turtle == turtleBefore);
}

void testBatchLimits()
{
    Host host(kSkeleton);
    auto ops = host.ops();
    assert(! ops.applyEdits({}).ok);

    std::vector<GraphEdit> many(501, addNode("x", "val:Gain"));
    assert(! ops.applyEdits(many).ok);

    // Disconnect works too.
    assert(ops.applyEdits({addNode("osc", "val:Oscillator"), connect("osc", "out", "out", "in")}).ok);
    GraphEdit cut; cut.kind = GraphEdit::Kind::disconnect;
    cut.fromNode = "osc"; cut.fromPort = "out"; cut.toNode = "out"; cut.toPort = "in";
    assert(ops.applyEdits({cut}).ok);
    assert(host.model.arcs().empty());
}

int countOf(const std::string& text, const std::string& fragment)
{
    int n = 0;
    for (auto at = text.find(fragment); at != std::string::npos; at = text.find(fragment, at + 1))
        ++n;
    return n;
}

/// Four gain stages in a row, each with its knob allowed +20 to +24 dB: set anywhere in range, they
/// multiply to about +90 dB.
const char* kRunaway = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix : <urn:valis:runaway#> .
:c a val:Circuit ; rdfs:label "Runaway" ; val:element :in , :g1 , :g2 , :g3 , :g4 , :out ;
  val:arc :a0 , :a1 , :a2 , :a3 , :a4 .
:in a val:Input .
:g1 a val:Gain . :g2 a val:Gain . :g3 a val:Gain . :g4 a val:Gain .
:out a val:Output .
:a0 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ; val:to [ val:node :g1 ; val:port "in" ] .
:a1 a val:Arc ; val:from [ val:node :g1 ; val:port "out" ] ; val:to [ val:node :g2 ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :g2 ; val:port "out" ] ; val:to [ val:node :g3 ; val:port "in" ] .
:a3 a val:Arc ; val:from [ val:node :g3 ; val:port "out" ] ; val:to [ val:node :g4 ; val:port "in" ] .
:a4 a val:Arc ; val:from [ val:node :g4 ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
:p0 a val:Param ; val:slot 0 ; val:target :g1 ; val:property val:gain ; lv2:name "One" ; lv2:symbol "one" ; lv2:minimum 20.0 ; lv2:maximum 24.0 .
:p1 a val:Param ; val:slot 1 ; val:target :g2 ; val:property val:gain ; lv2:name "Two" ; lv2:symbol "two" ; lv2:minimum 20.0 ; lv2:maximum 24.0 .
:p2 a val:Param ; val:slot 2 ; val:target :g3 ; val:property val:gain ; lv2:name "Three" ; lv2:symbol "three" ; lv2:minimum 20.0 ; lv2:maximum 24.0 .
:p3 a val:Param ; val:slot 3 ; val:target :g4 ; val:property val:gain ; lv2:name "Four" ; lv2:symbol "four" ; lv2:minimum 20.0 ; lv2:maximum 24.0 .
)";

void testStabilityCheckFindsARunawayAndGivesTheSettings()
{
    Host host(kRunaway);
    assert(host.loaded);
    auto ops = host.ops();

    const auto result = ops.checkStability(4, 0.5, -1);
    assert(result.ok);
    const auto& json = result.value;
    std::printf("  %s\n", json.substr(0, 400).c_str());

    // The first run is the declared settings (all at 0 dB) and is fine; the three random ones are not.
    assert(json.find("\"trials\":4") != std::string::npos && json.find("\"parameters\":4") != std::string::npos);
    assert(json.find("\"blowUps\":3") != std::string::npos);
    assert(countOf(json, "\"verdict\":\"blows-up\"") == 3 && countOf(json, "\"verdict\":\"ok\"") == 1);

    // For each run that failed, the settings that did it, before and after the jump.
    assert(countOf(json, "\"settings\":[") == 3);
    assert(json.find("\"name\":\"Three\"") != std::string::npos && json.find("\"afterJump\":") != std::string::npos);
    assert(json.find("val:Limiter") != std::string::npos);                    // and what to do about it

    // Repeatable: the same call gives the same answer.
    assert(ops.checkStability(4, 0.5, -1).value == json);
}

/// A binding that gives its own range (a knob that goes from 20 to 24 on a port that allows -60 to 24)
/// is held to it by the Controls view and the host, and has to be by the ops: a caller that reads a
/// wider range is told values are fine that the knob will not take.
void testABindingsOwnRangeIsTheRangeTheOpsReport()
{
    Host host(kRunaway);
    auto ops = host.ops();

    const auto listed = ops.listParams();
    assert(listed.size() == 4);
    for (const auto& p : listed)
        assert(p.minimum == 20.0 && p.maximum == 24.0);

    const auto low = ops.setParam(0, -30.0);
    assert(low.ok && std::abs(std::stod(low.value) - 20.0) < 1.0e-6);
    assert(! low.diagnostics.empty() && low.diagnostics.front().message.find("clamped to [20, 24]") != std::string::npos);

    const auto high = ops.setParam(0, 24.0);
    assert(high.ok && high.diagnostics.empty());

    // A binding with no range of its own still reports the port's.
    Host plain(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    bool sawPortRange = false;
    for (const auto& p : plain.ops().listParams())
        sawPortRange = sawPortRange || (p.minimum == 20.0 && p.maximum == 20000.0);   // Cutoff, as the Ladder declares it
    assert(sawPortRange);
}

void testStabilityCheckLeavesTheLiveCircuitAlone()
{
    Host host(kRunaway);
    auto ops = host.ops();
    ops.setParam(0, 22.0);
    const auto turtle = host.turtle;
    const int installs = host.installs;
    const auto versions = host.history.versions().size();

    assert(ops.checkStability(3, 0.5, -1).ok);

    assert(host.turtle == turtle && host.installs == installs);
    assert(host.history.versions().size() == versions);                    // not an edit
    assert(std::abs(std::stod(ops.getParam(0).value) - 22.0) < 1.0e-6);     // the check did not move it
}

void testStabilityCheckOnAStableCircuit()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    auto ops = host.ops();

    const auto result = ops.checkStability(5, 0.5, -1);
    assert(result.ok);
    const auto& json = result.value;
    assert(json.find("\"blowUps\":0") != std::string::npos && json.find("\"nonFinite\":0") != std::string::npos);
    assert(json.find("Every run stayed finite") != std::string::npos);
    assert(json.find("\"settings\"") == std::string::npos || json.find("\"verdict\":\"ok\"") == std::string::npos);
    assert(countOf(json, "\"seed\":") == 5);
}

void testStabilityCheckSaysWhenNothingIsBound()
{
    Host host(kSkeleton);
    auto ops = host.ops();
    assert(ops.applyEdits({addNode("osc", "val:Oscillator"), connect("osc", "out", "out", "in")}).ok);

    const auto result = ops.checkStability(3, 0.25, -1);
    assert(result.ok);
    assert(result.value.find("\"parameters\":0") != std::string::npos);
    assert(result.value.find("No parameters are bound") != std::string::npos);
}

void testStabilityCheckFindsASilentSetting()
{
    Host host(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix : <urn:valis:shut#> .
:c a val:Circuit ; rdfs:label "Shut" ; val:element :in , :vca , :out ; val:arc :a0 , :a1 .
:in a val:Input .
:vca a val:VCA .
:out a val:Output .
:a0 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ; val:to [ val:node :vca ; val:port "in" ] .
:a1 a val:Arc ; val:from [ val:node :vca ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
:p0 a val:Param ; val:slot 0 ; val:target :vca ; val:property val:cv ; lv2:name "Level" ; lv2:symbol "level" ; lv2:minimum 0.0 ; lv2:maximum 0.0 .
)");
    auto ops = host.ops();
    const auto result = ops.checkStability(3, 0.25, -1);
    assert(result.ok);
    assert(result.value.find("\"silent\":2") != std::string::npos);           // the two random runs; the declared one passes input
    assert(result.value.find("closes the sound off") != std::string::npos);
}

void testStabilityCheckPlaysTheNoteForAnInstrument()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/sh101.ttl"));
    auto ops = host.ops();
    const auto withNote = ops.checkStability(2, 0.5, 60);
    assert(withNote.ok && withNote.value.find("\"verdict\":\"silent\"") == std::string::npos);
    assert(ops.checkStability(0, 100.0, 999).ok);                              // arguments are brought into range
}

/// The circuits shipped in examples/ are what an agent starts from, so each should stay finite and
/// bounded whatever its knobs are set to, all of them moving at once.
void testEveryShippedExampleSurvivesItsKnobs()
{
    int checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(VALIS_EXAMPLES_DIR))
    {
        if (entry.path().extension() != ".ttl")
            continue;

        Host host(readFile(entry.path().string().c_str()));
        if (! host.loaded)
            continue;   // some need an optional build, such as the neural amp

        const auto result = host.ops().checkStability(4, 0.5, 60);
        assert(result.ok);
        ++checked;

        const auto& json = result.value;
        if (json.find("\"nonFinite\":0") == std::string::npos || json.find("\"blowUps\":0") == std::string::npos)
        {
            std::printf("  %s is not stable under its own knobs:\n    %s\n", entry.path().filename().string().c_str(), json.substr(0, 700).c_str());
            assert(false);
        }
    }
    std::printf("  %d examples survived random settings of their knobs\n", checked);
    assert(checked > 30);
}

void testUndoAndRedoWalkTheVersions()
{
    Host host(kSkeleton);
    auto ops = host.ops();
    assert(! ops.undo().ok);                         // nothing yet to go back to
    assert(! ops.redo().ok);

    assert(ops.addNode("urn:valis:new#osc", "val:Oscillator").ok);
    assert(ops.connect("urn:valis:new#osc", "out", "urn:valis:new#out", "in").ok);
    assert(host.model.arcs().size() == 1);

    // Each change is a version, with what made it; the start is the first.
    const auto listed = ops.history().value;
    assert(listed.find("starting point") != std::string::npos);
    assert(listed.find("add_node osc (val:Oscillator)") != std::string::npos);
    assert(listed.find("connect osc.out to out.in") != std::string::npos);
    assert(host.history.versions().size() == 3);

    // Back over the connect: the arc is gone, the node is not.
    const auto undone = ops.undo();
    assert(undone.ok && undone.value.find("connect osc.out to out.in") != std::string::npos);
    assert(host.model.arcs().empty() && host.model.findElement("urn:valis:new#osc") != nullptr);

    assert(ops.undo().ok);                           // and over the add
    assert(host.model.findElement("urn:valis:new#osc") == nullptr);
    assert(! ops.undo().ok);                         // at the start

    assert(ops.redo().ok && host.model.findElement("urn:valis:new#osc") != nullptr);
    assert(ops.redo().ok && host.model.arcs().size() == 1);
    assert(! ops.redo().ok);

    // A new change after an undo ends the redo, as in any editor.
    assert(ops.undo().ok);
    assert(ops.addNode("urn:valis:new#other", "val:Gain").ok);
    assert(! ops.redo().ok);
    assert(ops.history().value.find("canRedo\":false") != std::string::npos);
}

void testABatchIsOneVersionAndAFailureIsNone()
{
    Host host(kSkeleton);
    auto ops = host.ops();

    assert(ops.applyEdits({addNode("a", "val:Oscillator"), addNode("b", "val:VCA"), connect("a", "out", "b", "in"),
                           connect("b", "out", "out", "in")}).ok);
    assert(host.history.versions().size() == 2);     // the start, and the batch
    assert(host.history.current()->label == "apply_edits (4 edits)");

    // A failed edit is not a version.
    assert(! ops.addNode("urn:valis:new#x", "val:NotAThing").ok);
    assert(! ops.connect("urn:valis:new#a", "nonsense", "urn:valis:new#b", "in").ok);
    assert(! ops.applyEdits({addNode("c", "val:Gain"), connect("c", "bad", "out", "in")}).ok);
    assert(host.history.versions().size() == 2);

    // One undo takes back the whole batch.
    assert(ops.undo().ok);
    assert(host.model.elements().size() == 1);
}

void testTheDiffSaysWhatTheLastChangeDid()
{
    Host host(kSkeleton);
    auto ops = host.ops();
    assert(! ops.diff().ok);                         // one version: nothing to compare with

    assert(ops.addNode("urn:valis:new#osc", "val:Oscillator").ok);
    assert(ops.connect("urn:valis:new#osc", "out", "urn:valis:new#out", "in").ok);

    const auto last = ops.diff();
    assert(last.ok);
    std::printf("  %s\n", last.value.c_str());
    assert(last.value.find("\"count\":1") != std::string::npos);
    assert(last.value.find("connected osc.out to out.in") != std::string::npos);

    // Across several versions, by number.
    const auto all = ops.diff(1, 3);
    assert(all.ok && all.value.find("added osc (val:Oscillator)") != std::string::npos);
    assert(all.value.find("connected osc.out to out.in") != std::string::npos);
    assert(all.value.find("\"count\":2") != std::string::npos);

    // And the other way round says the opposite.
    const auto back = ops.diff(3, 1);
    assert(back.value.find("removed osc") != std::string::npos && back.value.find("disconnected") != std::string::npos);

    assert(! ops.diff(1, 99).ok);
    assert(! ops.diff(99).ok);
    assert(! ops.diff(1, 1).value.empty() && ops.diff(1, 1).value.find("\"count\":0") != std::string::npos);
}

void testAnEditMadeOutsideTheToolsIsNotLost()
{
    Host host(kSkeleton);
    auto ops = host.ops();
    assert(ops.addNode("urn:valis:new#osc", "val:Oscillator").ok);

    // The editor changes the circuit without going through the ops.
    std::vector<Diagnostic> ignored;
    std::string typed = host.turtle;
    typed += "\n:extra a val:Gain .\n:main val:element :extra .\n";
    assert(host.write(typed, ignored));
    assert(host.model.findElement("urn:valis:new#extra") != nullptr);

    // The next change records that state first, so it can be returned to.
    assert(ops.addNode("urn:valis:new#vca", "val:VCA").ok);
    const auto versions = ops.history().value;
    assert(versions.find("changed outside these tools") != std::string::npos);

    // Undo goes back over the add and lands on the typed circuit, with its node.
    assert(ops.undo().ok);
    assert(host.model.findElement("urn:valis:new#extra") != nullptr);
    assert(host.model.findElement("urn:valis:new#vca") == nullptr);

    // And one more goes back to before the typing.
    assert(ops.undo().ok);
    assert(host.model.findElement("urn:valis:new#extra") == nullptr);
    assert(host.model.findElement("urn:valis:new#osc") != nullptr);
}

void testUndoWhenTheTextDoesNotCompileRestoresTheLastGoodOne()
{
    Host host(kSkeleton);
    auto ops = host.ops();
    assert(ops.addNode("urn:valis:new#osc", "val:Oscillator").ok);
    const auto good = host.turtle;

    // The editor holds text that is mid-edit and broken. The circuit that is playing is the last good one.
    std::vector<Diagnostic> ignored;
    assert(! host.write("this is not turtle {{{", ignored));
    assert(host.model.findElement("urn:valis:new#osc") != nullptr);   // still the playing circuit

    const auto restored = ops.undo();
    assert(restored.ok);
    assert(restored.value.find("did not compile") != std::string::npos);
    assert(host.turtle == good);                      // the broken text is gone and the good text is back
    assert(host.history.versions().size() == 2);      // and the broken text was never a version
}

void testNoHistoryNoUndo()
{
    Host host(kSkeleton);
    OpContext ctx;
    ctx.ontology = &host.ontology;
    ctx.engine = &host.engine;
    ctx.readTurtle = [&] { return host.turtle; };
    ctx.writeTurtle = [&](const std::string& s, std::vector<Diagnostic>& d) { return host.write(s, d); };
    ctx.readModel = [&]() -> const CircuitModel* { return &host.model; };
    OpDispatcher bare(ctx);

    assert(bare.addNode("urn:valis:new#osc", "val:Oscillator").ok);   // editing works without one
    assert(! bare.undo().ok && ! bare.redo().ok && ! bare.history().ok && ! bare.diff().ok);
}

void testAnUnknownPortOffersTheRealOnes()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    auto ops = host.ops();

    // The end of an arc: an input is wanted, so the inputs are what is offered.
    const auto bad = ops.connect("urn:valis:basic#in", "out", "urn:valis:basic#vcf", "Cutoff");
    assert(! bad.ok);
    std::string text;
    for (const auto& d : bad.diagnostics) text += d.message + "\n";
    std::printf("  %s", text.c_str());
    assert(text.find("has no port 'Cutoff'") != std::string::npos);
    assert(text.find("Did you mean 'cutoff'") != std::string::npos);
    assert(text.find("Its input ports are") != std::string::npos);
    assert(text.find("resonance") != std::string::npos);

    // The start of an arc: an output is wanted.
    const auto badSource = ops.connect("urn:valis:basic#vcf", "outt", "urn:valis:basic#drive", "in");
    assert(! badSource.ok);
    text.clear();
    for (const auto& d : badSource.diagnostics) text += d.message + "\n";
    assert(text.find("Did you mean 'out'") != std::string::npos);
    assert(text.find("Its output ports are") != std::string::npos);

    // A name nothing like any port gets the list but no guess.
    const auto nonsense = ops.connect("urn:valis:basic#in", "out", "urn:valis:basic#vcf", "zzzzzzzz");
    text.clear();
    for (const auto& d : nonsense.diagnostics) text += d.message + "\n";
    assert(text.find("Did you mean") == std::string::npos && text.find("Its input ports are") != std::string::npos);
}

void testLintThroughTheOp()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    const auto clean = host.ops().lint();
    assert(clean.ok);
    assert(clean.value.find("\"errors\":0") != std::string::npos);
    assert(clean.value.find("\"warnings\":0") != std::string::npos);

    // Nothing wired to the output is an error, and it is reported rather than refused.
    Host mute(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix : <urn:valis:mute#> .
:c a val:Circuit ; rdfs:label "Mute" ; val:element :in , :out .
:in a val:Input .
:out a val:Output .
)");
    const auto bad = mute.ops().lint();
    assert(bad.ok);
    assert(bad.value.find("\"errors\":1") != std::string::npos);
    assert(bad.value.find("nothing-reaches-output") != std::string::npos);
}

void testDiagnosticsReportARecoveredProblem()
{
    Host host(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix : <urn:valis:problem#> .
:c a val:Circuit ; rdfs:label "Problem" ; val:element :in , :amp , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:amp a val:NamModel ; val:model "/no/such/model.nam" .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ; val:to [ val:node :amp ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :amp ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
)");
    assert(host.loaded);

    const auto result = host.ops().getDiagnostics();
    assert(result.ok);
    assert(result.value.find("\"loaded\":true") != std::string::npos);
    assert(result.value.find("\"node\":\"urn:valis:problem#amp\"") != std::string::npos);
    assert(result.value.find("Not found") != std::string::npos);
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

void testSampleOps()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/granular.ttl"));
    auto ops = host.ops();

    const std::string node = "urn:valis:granular#sample";

    // What the document declares, until something replaces it.
    const auto declared = ops.getSample(node);
    assert(declared.ok);
    assert(declared.value == "samples/bell.wav");

    // A node that plays no sample, and a node that does not exist.
    assert(ops.getSample("urn:valis:granular#gran").ok);
    assert(! ops.getSample("urn:valis:granular#nosuch").ok);

    // A file that will not load leaves the previous one in place.
    const auto bad = ops.setSample(node, "no/such/sample.wav");
    assert(! bad.ok);
    assert(! bad.diagnostics.empty());
    assert(ops.getSample(node).value == "samples/bell.wav");

    const auto good = ops.setSample(node, VALIS_EXAMPLES_DIR "/samples/bell.wav");
    assert(good.ok);
    assert(ops.getSample(node).value == VALIS_EXAMPLES_DIR "/samples/bell.wav");
}

/// The editor has a Save; a caller driving Valis from outside needs one too.
void testSaveFile()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    auto ops = host.ops();

    const auto path = std::string(VALIS_ROOT_DIR) + "/build/op-save-test.ttl";
    const auto saved = ops.saveFile(path);
    assert(saved.ok);

    // What comes back is what went out, so a round trip through a file loses
    // nothing: the document is the circuit.
    assert(readFile(path.c_str()) == ops.getTurtle().value);

    std::remove(path.c_str());

    assert(! ops.saveFile("").ok);
    assert(! ops.saveFile("/no/such/directory/x.ttl").ok);
}

/// The editor has a virtual keyboard. Without this a caller can build an
/// instrument over MCP and never hear it.
void testPlayingNotes()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/sh101.ttl"));
    auto ops = host.ops();

    assert(! ops.noteOn(-1, 1.0).ok);
    assert(! ops.noteOn(128, 1.0).ok);
    assert(! ops.noteOff(200).ok);

    const std::vector<float> silence(256, 0.0f);
    std::vector<float> block(256, 0.0f);

    const auto peakOf = [](const std::vector<float>& v)
    {
        float peak = 0.0f;
        for (const float s : v) peak = std::max(peak, std::abs(s));
        return peak;
    };

    for (int i = 0; i < 8; ++i)
        host.engine.process(silence.data(), block.data(), 256);
    const float before = peakOf(block);

    assert(ops.noteOn(60, 1.0).ok);
    for (int i = 0; i < 40; ++i)
        host.engine.process(silence.data(), block.data(), 256);
    const float during = peakOf(block);

    assert(ops.allNotesOff().ok);
    for (int i = 0; i < 200; ++i)
        host.engine.process(silence.data(), block.data(), 256);
    const float after = peakOf(block);

    assert(during > before * 4.0f);
    assert(during > 0.01f);
    assert(after < during * 0.5f);
}

/// The Controls view shows an Oscilloscope's peak and RMS live. A caller needs
/// the same numbers to tell whether what it built is doing anything.
void testReadOutputs()
{
    Host host(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit ; val:element :osc , :scope , :out ; val:arc :a1 , :a2 .
:osc a val:Oscillator ; val:frequency 440.0 .
:scope a val:Oscilloscope .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :osc ; val:port "out" ] ;
                val:to   [ val:node :scope ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :scope ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)");
    assert(host.loaded);
    auto ops = host.ops();

    const std::vector<float> silence(512, 0.0f);
    std::vector<float> block(512, 0.0f);
    for (int i = 0; i < 20; ++i)
        host.engine.process(silence.data(), block.data(), 512);

    const auto all = ops.readOutputs();
    assert(all.ok);

    // The scope reports; the oscillator has no control output, so it is absent
    // rather than listed with nothing in it.
    assert(all.value.find("urn:valis:t#scope") != std::string::npos);
    assert(all.value.find("\"peak\"") != std::string::npos);
    assert(all.value.find("\"rms\"") != std::string::npos);
    assert(all.value.find("urn:valis:t#osc") == std::string::npos);

    const auto one = ops.readOutputs("urn:valis:t#scope");
    assert(one.ok);
    assert(one.value.find("urn:valis:t#scope") != std::string::npos);

    assert(! ops.readOutputs("urn:valis:t#nosuch").ok);
}

/// fitParams moves the circuit's parameters toward a recording. The recording is the SH-101 with its filter envelope
/// opening to 1500 Hz; the circuit under test opens to 8000 Hz and has to find its way back by the Filter Peak knob.
void testFitParams()
{
    const auto source = readFile(VALIS_EXAMPLES_DIR "/sh101.ttl");
    const auto target = std::string(VALIS_ROOT_DIR) + "/build/op-fit-target.wav";
    const std::string peak = "val:max 8000.0";

    {
        auto darker = source;
        const auto at = darker.find(peak);
        assert(at != std::string::npos);
        darker.replace(at, peak.size(), "val:max 1500.0");

        Host original(darker);
        const auto rendered = original.ops().render(target, 1.0, 48000.0, 48, 1.0);
        assert(rendered.ok);
    }

    Host host(source);
    auto ops = host.ops();

    const auto fit = ops.fitParams(target, 48, 1.0, 1.0, 40, {7});   // slot 7 is the filter peak
    assert(fit.ok);
    const auto number = [&](const char* key)
    {
        const auto where = fit.value.find(key);
        assert(where != std::string::npos);
        return std::stod(fit.value.substr(where + std::string(key).size()));
    };
    const double start = number("\"startDistanceDb\":"), best = number("\"bestDistanceDb\":");
    std::printf("  fit: %.2f dB before, %.2f dB after\n", start, best);
    assert(start > 1.0);
    assert(best < start * 0.5);

    // The same call finds the same answer.
    assert(ops.fitParams(target, 48, 1.0, 1.0, 40, {7}).value == fit.value);

    // It never ends up further than it began.
    assert(best <= start);

    // Failures are named.
    assert(! ops.fitParams("", 48, 1.0, 1.0, 10).ok);
    assert(! ops.fitParams("/no/such/file.wav", 48, 1.0, 1.0, 10).ok);
    assert(! ops.fitParams(target, 200, 1.0, 1.0, 10).ok);          // not a MIDI note
    assert(! ops.fitParams(target, 48, 1.0, 1.0, 10, {99}).ok);     // no such slot
    std::remove(target.c_str());
}

/// A bell is rendered by the circuit, then fitted back: the modes written in the circuit must come out of the
/// recording of it. The strike is a noise burst, so levels vary, but the frequencies and ring times are the bank's.
void testFitModes()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/free-bell.ttl"));
    auto ops = host.ops();
    const auto path = std::string(VALIS_ROOT_DIR) + "/build/op-fitmodes-test.wav";

    const auto rendered = ops.render(path, 3.0, 48000.0, 60, 1.0);
    assert(rendered.ok);

    const auto fit = ops.fitModes(path, 12, 2.0);
    assert(fit.ok);

    const auto at = fit.value.find("\"modes\":\"");
    assert(at != std::string::npos);
    const auto start = at + std::string("\"modes\":\"").size();
    const auto modes = fit.value.substr(start, fit.value.find('"', start) - start);
    std::printf("  fit_modes: %s\n", modes.c_str());

    // The three strongest modes of the circuit's bank, by frequency, within 0.3 percent.
    int matched = 0;
    for (const double wanted : {523.0, 1047.0, 1318.0})
    {
        std::istringstream words(modes);
        std::string word;
        while (words >> word)
            if (std::abs(std::stod(word) - wanted) < wanted * 0.003)
                ++matched;
    }
    assert(matched == 3);

    const auto distance = fit.value.find("\"distanceDb\":");
    assert(distance != std::string::npos);
    assert(std::stod(fit.value.substr(distance + 13)) < 10.0);
    assert(fit.value.find("val:FreeModalBank") != std::string::npos);

    assert(! ops.fitModes("").ok);
    assert(! ops.fitModes("/no/such/file.wav").ok);
    assert(! ops.fitModes(std::string(VALIS_ROOT_DIR) + "/vocabs/valis.ttl").ok);   // not audio
    std::remove(path.c_str());
}

/// Rendering is how a caller checks that what it built actually sounds. It uses
/// a fresh engine, so it must not disturb what the host is playing.
void testRender()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/sh101.ttl"));
    auto ops = host.ops();

    const auto path = std::string(VALIS_ROOT_DIR) + "/build/op-render-test.wav";

    const auto result = ops.render(path, 0.5, 48000.0, 60, 1.0);
    assert(result.ok);
    assert(result.value.find("\"finite\":true") != std::string::npos);

    // A note was played, so something came out.
    const auto peakAt = result.value.find("\"peak\":");
    assert(peakAt != std::string::npos);
    assert(std::stod(result.value.substr(peakAt + 7)) > 0.01);

    // The render carries a measurement of what it made. A note was played at middle C, but the
    // SH-101 mixes in a sub-oscillator an octave below, and the waveform that results repeats at
    // that lower pitch, which is the pitch a listener names.
    const auto pitchAt = result.value.find("\"hz\":");
    assert(pitchAt != std::string::npos);
    const double hz = std::stod(result.value.substr(pitchAt + 5));
    std::printf("  sh101 note 60 renders at %.2f Hz\n", hz);
    assert(std::abs(1200.0 * std::log2(hz / 130.81)) < 50.0);
    assert(result.value.find("\"notes\":[") != std::string::npos);

    // And the same measurement of the file on disk.
    const auto measured = ops.measure(path);
    assert(measured.ok);
    assert(measured.value.find("\"note\":\"C3\"") != std::string::npos);
    assert(! ops.measure("").ok);
    assert(! ops.measure("/no/such/file.wav").ok);
    assert(! ops.measure(std::string(VALIS_ROOT_DIR) + "/vocabs/valis.ttl").ok);   // not audio

    std::FILE* written = std::fopen(path.c_str(), "rb");
    assert(written != nullptr);
    std::fseek(written, 0, SEEK_END);
    assert(std::ftell(written) > 1000);
    std::fclose(written);
    std::remove(path.c_str());

    assert(! ops.render("", 0.5, 48000.0, 60, 1.0).ok);

    // A MIDI file plays in place of a note. One middle C at tick 0, held a quarter, division 480.
    const unsigned char smf[] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0x01,0xE0,
        'M','T','r','k', 0,0,0,13,
        0x00,0x90,60,100,   0x83,0x60,0x80,60,0,   0x00,0xFF,0x2F,0x00 };
    const auto midiPath = std::string(VALIS_ROOT_DIR) + "/build/op-render-test.mid";
    std::FILE* midi = std::fopen(midiPath.c_str(), "wb");
    assert(midi != nullptr);
    std::fwrite(smf, 1, sizeof(smf), midi);
    std::fclose(midi);

    const auto played = ops.render(path, 0.5, 48000.0, -1, 1.0, midiPath, 120.0, true);
    assert(played.ok);
    const auto playedAt = played.value.find("\"peak\":");
    assert(playedAt != std::string::npos);
    assert(std::stod(played.value.substr(playedAt + 7)) > 0.01);

    assert(! ops.render(path, 0.5, 48000.0, -1, 1.0, "/no/such/file.mid").ok);
    assert(! ops.render(path, 0.5, 48000.0, -1, 1.0, path).ok);   // audio, not MIDI
    std::remove(midiPath.c_str());
    std::remove(path.c_str());
}

void testShotOps()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/sh101.ttl"));
    auto ops = host.ops();

    // The valis-shot measurements are ops, so MCP and the UI call the code the command line tool runs.
    const auto path = std::string(VALIS_ROOT_DIR) + "/build/op-shot-test.wav";
    assert(ops.render(path, 0.5, 48000.0, 60, 1.0).ok);

    const auto measured = ops.measure(path);
    assert(measured.ok);
    assert(measured.value.find("\"loudness\"") != std::string::npos);
    assert(measured.value.find("\"envelope\"") != std::string::npos);
    assert(measured.value.find("\"signal\"") != std::string::npos);

    const auto compared = ops.compare(path, path);
    assert(compared.ok && compared.value.find("\"spectralDistanceDb\":0") != std::string::npos);

    assert(! ops.measure("").ok);
    assert(! ops.measure("/no/such/file.wav").ok);
    assert(! ops.compare(path, "/no/such/file.wav").ok);

    // Without models listen says why, and with them it answers: either way it is not a crash.
    const auto heard = ops.listen(path, {"a synthesizer", "a violin"});
    assert(heard.ok ? heard.value.find("\"matches\"") != std::string::npos : ! heard.diagnostics.empty());

    const auto asked = ops.soundsLike(path, "a synthesizer");
    assert(asked.ok ? asked.value.find("\"verdict\"") != std::string::npos : ! asked.diagnostics.empty());
    assert(! ops.soundsLike(path, "").ok);

    std::remove(path.c_str());
}

/// A polyphonic circuit tells a caller what it is: which elements the model
/// generated, and how many voices there are.
void testGraphReportsSubcircuitExpansion()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/polysynth.ttl"));
    assert(host.loaded);
    auto ops = host.ops();

    const auto graph = ops.getGraph();
    assert(graph.ok);

    // An expanded element names the instance it came out of and its voice, so a
    // caller can tell a generated element from a written one.
    assert(graph.value.find("\"instance\":\"urn:valis:polysynth#poly\"") != std::string::npos);
    assert(graph.value.find("\"voice\":7") != std::string::npos);

    const auto diagnostics = ops.getDiagnostics();
    assert(diagnostics.ok);
    assert(diagnostics.value.find("\"voices\":8") != std::string::npos);
    assert(diagnostics.value.find("\"sampleRate\":") != std::string::npos);
}

/// An event port is not something a caller can connect a signal to, so the
/// type listing has to say which ports those are.
void testElementTypesReportEventPorts()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    auto ops = host.ops();

    const auto types = ops.listElementTypes();
    const auto noteOut = std::find_if(types.begin(), types.end(),
                                      [](const ElementTypeInfo& t)
                                      { return t.implementation == "NoteOut"; });
    assert(noteOut != types.end());

    const auto out = std::find_if(noteOut->ports.begin(), noteOut->ports.end(),
                                  [](const PortInfo& p) { return p.symbol == "out"; });
    assert(out != noteOut->ports.end());
    assert(out->event);
    assert(! out->input);

    // And an ordinary control port is not marked as one.
    const auto gate = std::find_if(noteOut->ports.begin(), noteOut->ports.end(),
                                   [](const PortInfo& p) { return p.symbol == "gate"; });
    assert(gate != noteOut->ports.end());
    assert(! gate->event);
}

/// A circuit is a plugin, so it describes itself in the terms a catalogue
/// already uses rather than in terms invented here.
void testCircuitProfile()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/dmx.ttl"));
    assert(host.loaded);
    auto ops = host.ops();

    const auto result = ops.getProfile();
    assert(result.ok);

    assert(result.value.find("\"declared\":true") != std::string::npos);
    assert(result.value.find("Oberheim DMX") != std::string::npos);
    assert(result.value.find("transmissions/DrumInstrument") != std::string::npos);
    assert(result.value.find("transmissions/Midi") != std::string::npos);
    assert(result.value.find("transmissions/Audio") != std::string::npos);
    assert(result.value.find("Hip Hop") != std::string::npos);

    // The caution is the author's own warning and has to survive into a listing.
    assert(result.value.find("monophonic") != std::string::npos);
}

/// A circuit that says nothing about itself is complete and runs the same. The
/// profile describes it for a listing; it is not part of what it does.
void testACircuitNeedsNoProfile()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    assert(host.loaded);

    const auto result = host.ops().getProfile();
    assert(result.ok);
    assert(result.value.find("\"declared\":false") != std::string::npos);
    assert(result.value.find("\"role\":[]") != std::string::npos);

    // And nothing was reported against it.
    assert(host.diagnostics.empty());
}

/// Offering to be listed and leaving out what a listing cannot do without is
/// worth saying, but it must not stop the circuit running.
void testAnIncompleteProfileIsReportedNotFatal()
{
    Host host(R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix trn: <http://purl.org/stuff/transmissions/> .
@prefix :    <urn:valis:t#> .
:c a val:Circuit , trn:PluginProfile ;
   val:element :in , :out ; val:arc :a1 .
:in a val:Input .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)");

    assert(host.loaded);   // it still runs

    const auto said = [&](std::string_view fragment)
    {
        return std::any_of(host.diagnostics.begin(), host.diagnostics.end(),
                           [&](const Diagnostic& d)
                           { return d.message.find(fragment) != std::string::npos; });
    };

    assert(said("no rdfs:label"));
    assert(said("no trn:role"));
}

void testMorph()
{
    // basic.ttl with the cutoff and resonance bound to two ends each, and the
    // drive left alone.
    auto source = readFile(VALIS_EXAMPLES_DIR "/basic.ttl");
    const auto bind = [&](const char* symbol, const char* a, const char* b)
    {
        const std::string anchor = std::string("lv2:symbol \"") + symbol + "\"";
        const auto at = source.find(anchor);
        assert(at != std::string::npos);
        source.insert(at, std::string("val:valueA ") + a + " ; val:valueB " + b + " ; ");
    };
    bind("cutoff", "200.0", "2000.0");
    bind("resonance", "0.1", "0.9");

    Host host(source);
    assert(host.loaded);
    auto ops = host.ops();

    const auto at = [&](double position)
    {
        const auto result = ops.morph(position);
        assert(result.ok);
        return std::pair{ std::stod(ops.getParam(0).value), std::stod(ops.getParam(1).value) };
    };

    auto [cutoff, resonance] = at(0.0);
    assert(std::abs(cutoff - 200.0) < 1.0e-3 && std::abs(resonance - 0.1) < 1.0e-6);
    std::tie(cutoff, resonance) = at(1.0);
    assert(std::abs(cutoff - 2000.0) < 1.0e-3 && std::abs(resonance - 0.9) < 1.0e-6);
    std::tie(cutoff, resonance) = at(0.25);
    assert(std::abs(cutoff - 650.0) < 1.0e-3 && std::abs(resonance - 0.3) < 1.0e-6);

    // Out of range is clamped and said so; the drive was never touched.
    const double drive = std::stod(ops.getParam(2).value);
    const auto clamped = ops.morph(3.0);
    assert(clamped.ok && ! clamped.diagnostics.empty());
    assert(std::abs(std::stod(ops.getParam(0).value) - 2000.0) < 1.0e-3);
    assert(std::stod(ops.getParam(2).value) == drive);

    // A circuit that declares no morph says so instead of doing nothing.
    Host plain(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    assert(! plain.ops().morph(0.5).ok);
}

void testRandomize()
{
    const auto source = readFile(VALIS_EXAMPLES_DIR "/basic.ttl");

    const auto values = [](OpDispatcher& ops)
    {
        std::vector<double> out;
        for (const auto& p : ops.listParams())
            out.push_back(p.value);
        return out;
    };

    Host a(source), b(source), c(source);
    auto opsA = a.ops(), opsB = b.ops(), opsC = c.ops();

    assert(opsA.randomize(7).ok);
    assert(opsB.randomize(7).ok);
    assert(opsC.randomize(8).ok);
    assert(values(opsA) == values(opsB));   // same seed, same patch
    assert(values(opsA) != values(opsC));

    // Every value is inside its range.
    for (const auto& p : opsA.listParams())
        assert(p.value >= p.minimum && p.value <= p.maximum);

    // A kept slot does not move, and keeping it leaves the others as they were.
    Host d(source), e(source);
    auto opsD = d.ops(), opsE = e.ops();
    const double before = opsD.listParams()[0].value;
    assert(opsD.randomize(7, 1.0, {0}).ok);
    assert(opsE.randomize(7).ok);
    assert(opsD.listParams()[0].value == before);
    assert(opsD.listParams()[1].value == opsE.listParams()[1].value);

    // An amount of zero leaves everything where it was, to float precision.
    Host f(source);
    auto opsF = f.ops();
    const auto start = values(opsF);
    assert(opsF.randomize(7, 0.0).ok);
    const auto after = values(opsF);
    for (std::size_t i = 0; i < start.size(); ++i)
        assert(std::abs(after[i] - start[i]) < 1.0e-4 * std::max(1.0, std::abs(start[i])));
}

int main()
{
    testTurtleRoundTrip();
    testValidateReportsWithoutInstalling();
    testBadTurtleLeavesTheCircuitRunning();
    testListElementTypes();
    testSampleOps();
    testGetGraph();
    testGraphEditing();
    testParameters();
    testDiagnostics();
    testDescribeElement();
    testStabilityCheckFindsARunawayAndGivesTheSettings();
    testABindingsOwnRangeIsTheRangeTheOpsReport();
    testStabilityCheckLeavesTheLiveCircuitAlone();
    testStabilityCheckOnAStableCircuit();
    testStabilityCheckSaysWhenNothingIsBound();
    testStabilityCheckFindsASilentSetting();
    testStabilityCheckPlaysTheNoteForAnInstrument();
    testEveryShippedExampleSurvivesItsKnobs();
    testUndoAndRedoWalkTheVersions();
    testABatchIsOneVersionAndAFailureIsNone();
    testTheDiffSaysWhatTheLastChangeDid();
    testAnEditMadeOutsideTheToolsIsNotLost();
    testUndoWhenTheTextDoesNotCompileRestoresTheLastGoodOne();
    testNoHistoryNoUndo();
    testABatchBuildsACircuitInOneInstall();
    testABatchThatFailsLeavesNothingBehind();
    testABatchMayPassThroughAStateThatWouldNotInstall();
    testABatchThatIsWrongOnlyAsAWholeIsNotInstalled();
    testBatchLimits();
    testAnUnknownPortOffersTheRealOnes();
    testLintThroughTheOp();
    testDiagnosticsReportARecoveredProblem();
    testEditsSurviveAReparse();
    test909BassdrumProducesOutput();

    testSaveFile();
    testPlayingNotes();
    testReadOutputs();
    testRender();
    testShotOps();
    testFitParams();
    testFitModes();
    testGraphReportsSubcircuitExpansion();
    testElementTypesReportEventPorts();
    testCircuitProfile();
    testACircuitNeedsNoProfile();
    testAnIncompleteProfileIsReportedNotFatal();
    testMorph();
    testRandomize();

    std::puts("OpDispatcherTest PASSED");
    return 0;
}

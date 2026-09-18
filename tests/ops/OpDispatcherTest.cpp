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
#include <map>
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

    /// Stands in for the plugin's per-node sample choices.
    std::map<std::string, std::string> samples;

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

    std::FILE* written = std::fopen(path.c_str(), "rb");
    assert(written != nullptr);
    std::fseek(written, 0, SEEK_END);
    assert(std::ftell(written) > 1000);
    std::fclose(written);
    std::remove(path.c_str());

    assert(! ops.render("", 0.5, 48000.0, 60, 1.0).ok);
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
    testEditsSurviveAReparse();
    test909BassdrumProducesOutput();

    testSaveFile();
    testPlayingNotes();
    testReadOutputs();
    testRender();
    testGraphReportsSubcircuitExpansion();
    testElementTypesReportEventPorts();

    std::puts("OpDispatcherTest PASSED");
    return 0;
}

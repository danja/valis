// tests/compiler/CircuitCompilerTest.cpp

#include "valis/CircuitCompiler.h"
#include "valis/CircuitModel.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

const Ontology& shippedOntology()
{
    static const Ontology ontology = [] {
        Ontology o;
        std::vector<std::string> errors;
        const bool ok = o.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(ok && errors.empty());
        return o;
    }();
    return ontology;
}

struct Result
{
    CircuitModel model;
    CompiledCircuit compiled;
    std::vector<Diagnostic> diagnostics;
    bool built    = false;
    bool compiled_ = false;

    bool hasDiagnosticContaining(std::string_view fragment) const
    {
        return std::any_of(diagnostics.begin(), diagnostics.end(),
                           [&](const Diagnostic& d)
                           { return d.message.find(fragment) != std::string::npos; });
    }

    void dump() const
    {
        for (const auto& d : diagnostics)
            std::printf("    %s\n", d.toString().c_str());
    }
};

Result run(std::string_view turtle)
{
    Result result;

    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    const bool parsed = store.parse(turtle, "urn:valis:test", parseErrors);
    assert(parsed);

    result.built = result.model.build(store, shippedOntology(), result.diagnostics);
    if (result.built)
    {
        CircuitCompiler compiler;
        result.compiled_ = compiler.compile(result.model, shippedOntology(),
                                            result.compiled, result.diagnostics);
    }
    return result;
}

Result runFile(const char* path)
{
    Result result;

    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    const bool parsed = store.parseFile(path, parseErrors);
    assert(parsed);

    result.built = result.model.build(store, shippedOntology(), result.diagnostics);
    if (result.built)
    {
        CircuitCompiler compiler;
        result.compiled_ = compiler.compile(result.model, shippedOntology(),
                                            result.compiled, result.diagnostics);
    }
    return result;
}

const char* kPrefixes = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:t#> .
)";

std::string circuit(const std::string& body)
{
    return std::string(kPrefixes) + body;
}

// -- valid -----------------------------------------------------------------

void testSimpleChainCompiles()
{
    auto r = run(circuit(R"(
:c a val:Circuit ; val:element :in , :g , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:g  a val:Gain ; val:gain -3.0 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :g  ; val:port "in"  ] .
:a2 a val:Arc ; val:from [ val:node :g  ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    if (! r.compiled_) r.dump();
    assert(r.compiled_);
    assert(r.diagnostics.empty());
    assert(r.compiled.nodes.size() == 3);
    assert(r.compiled.audioLinks.size() == 2);
    assert(r.compiled.isValid());

    // Execution order must respect the arcs: in before g before out.
    const auto position = [&](std::string_view id) {
        for (std::size_t i = 0; i < r.compiled.nodes.size(); ++i)
            if (vocab::shortName(r.compiled.nodes[i].id) == id)
                return static_cast<int>(i);
        return -1;
    };
    assert(position("in") < position("g"));
    assert(position("g") < position("out"));
    assert(r.compiled.outputNode == position("out"));

    // A property set in the Turtle overrides the ontology default.
    const auto& gain = r.compiled.nodes[static_cast<std::size_t>(position("g"))];
    assert(gain.controlValues.size() == 1);
    assert(gain.controlValues[0] == -3.0);
}

void testDefaultsComeFromTheOntology()
{
    auto r = run(circuit(R"(
:c a val:Circuit ; val:element :in , :f , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:f  a val:Ladder .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :f  ; val:port "in"  ] .
:a2 a val:Arc ; val:from [ val:node :f  ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    assert(r.compiled_);
    const auto& filter = *std::find_if(r.compiled.nodes.begin(), r.compiled.nodes.end(),
                                       [](const auto& n) { return n.implementation == "Ladder"; });
    // cutoff, resonance, drive - in the ontology's declaration order.
    assert(filter.controlValues.size() == 3);
    assert(filter.controlValues[0] == 1000.0);   // lv2:default
}

void testFeedbackThroughUnitDelayIsAccepted()
{
    auto r = run(circuit(R"(
:c a val:Circuit ; val:element :in , :m , :g , :z , :out ;
   val:arc :a1 , :a2 , :a3 , :a4 , :a5 .
:in a val:Input .
:m  a val:Mixer .
:g  a val:Gain ; val:gain -6.0 .
:z  a val:UnitDelay .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :m  ; val:port "in"  ] .
:a2 a val:Arc ; val:from [ val:node :m  ; val:port "out" ] ;
                val:to   [ val:node :g  ; val:port "in"  ] .
:a3 a val:Arc ; val:from [ val:node :g  ; val:port "out" ] ;
                val:to   [ val:node :z  ; val:port "in"  ] .
:a4 a val:Arc ; val:from [ val:node :z  ; val:port "out" ] ;
                val:to   [ val:node :m  ; val:port "in"  ] .
:a5 a val:Arc ; val:from [ val:node :g  ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    if (! r.compiled_) r.dump();
    assert(r.compiled_);
    assert(r.diagnostics.empty());
}

// -- invalid ---------------------------------------------------------------

void testFeedbackWithoutUnitDelayIsRejected()
{
    auto r = run(circuit(R"(
:c a val:Circuit ; val:element :in , :m , :g , :out ; val:arc :a1 , :a2 , :a3 , :a4 .
:in a val:Input .
:m  a val:Mixer .
:g  a val:Gain .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :m  ; val:port "in"  ] .
:a2 a val:Arc ; val:from [ val:node :m  ; val:port "out" ] ;
                val:to   [ val:node :g  ; val:port "in"  ] .
:a3 a val:Arc ; val:from [ val:node :g  ; val:port "out" ] ;
                val:to   [ val:node :m  ; val:port "in"  ] .
:a4 a val:Arc ; val:from [ val:node :g  ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    assert(! r.compiled_);
    assert(r.hasDiagnosticContaining("no val:UnitDelay"));
    // The message must name the loop so the user can find it.
    assert(r.hasDiagnosticContaining("->"));
}

void testUnknownPortIsNamed()
{
    auto r = run(circuit(R"(
:c a val:Circuit ; val:element :in , :out ; val:arc :a1 .
:in a val:Input .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "nosuch" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    assert(! r.compiled_);
    assert(r.hasDiagnosticContaining("has no port 'nosuch'"));
}

void testDirectionAndRateAreChecked()
{
    // Arc into an output port.
    auto backwards = run(circuit(R"(
:c a val:Circuit ; val:element :in , :out ; val:arc :a1 .
:in a val:Input .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :in ; val:port "out" ] .
)"));
    assert(! backwards.compiled_);
    assert(backwards.hasDiagnosticContaining("which is an output port"));

    // Audio output into a control input.
    auto mixed = run(circuit(R"(
:c a val:Circuit ; val:element :in , :f , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:f  a val:Ladder .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :f  ; val:port "cutoff" ] .
:a2 a val:Arc ; val:from [ val:node :f  ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));
    assert(! mixed.compiled_);
    assert(mixed.hasDiagnosticContaining("audio output to a control input"));
}

void testSilentFanInIsRejected()
{
    auto r = run(circuit(R"(
:c a val:Circuit ; val:element :in , :o , :g , :out ; val:arc :a1 , :a2 , :a3 .
:in a val:Input .
:o  a val:Oscillator .
:g  a val:Gain .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :g  ; val:port "in"  ] .
:a2 a val:Arc ; val:from [ val:node :o  ; val:port "out" ] ;
                val:to   [ val:node :g  ; val:port "in"  ] .
:a3 a val:Arc ; val:from [ val:node :g  ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    assert(! r.compiled_);
    assert(r.hasDiagnosticContaining("only val:Mixer sums"));
}

void testAbstractAndUnknownClassesAreRejected()
{
    auto abstract = run(circuit(R"(
:c a val:Circuit ; val:element :x , :out ; val:arc :a1 .
:x a val:Transfer .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :x ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));
    assert(abstract.hasDiagnosticContaining("unknown or abstract element class Transfer"));

    // val:Network is declared as the seam for component-level work, and is not
    // instantiable yet.
    auto network = run(circuit(R"(
:c a val:Circuit ; val:element :n , :out ; val:arc :a1 .
:n a val:Network .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :n ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));
    assert(network.hasDiagnosticContaining("unknown or abstract element class Network"));
}

void testOutputCardinality()
{
    auto none = run(circuit(R"(
:c a val:Circuit ; val:element :in .
:in a val:Input .
)"));
    assert(! none.compiled_);
    assert(none.hasDiagnosticContaining("no val:Output"));

    auto two = run(circuit(R"(
:c a val:Circuit ; val:element :in , :o1 , :o2 ; val:arc :a1 , :a2 .
:in a val:Input .
:o1 a val:Output .
:o2 a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :o1 ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :o2 ; val:port "in" ] .
)"));
    assert(! two.compiled_);
    assert(two.hasDiagnosticContaining("it needs exactly one"));
}

void testDuplicateAndDanglingArcs()
{
    auto duplicate = run(circuit(R"(
:c a val:Circuit ; val:element :in , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));
    assert(! duplicate.compiled_);
    assert(duplicate.hasDiagnosticContaining("duplicate arc"));

    auto dangling = run(circuit(R"(
:c a val:Circuit ; val:element :in , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :ghost ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));
    assert(! dangling.compiled_);
    assert(dangling.hasDiagnosticContaining("not an element of this circuit"));
}

void testArcDeclaredButNotClaimed()
{
    auto r = run(circuit(R"(
:c a val:Circuit ; val:element :in , :out ; val:arc :a1 .
:in a val:Input .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
:orphan a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                    val:to   [ val:node :out ; val:port "in" ] .
)"));
    assert(r.hasDiagnosticContaining("declared but not listed"));
}

// -- the acceptance demo ---------------------------------------------------

void testSkreamCompiles()
{
    auto r = runFile(VALIS_EXAMPLES_DIR "/skream.ttl");

    if (! r.compiled_) r.dump();
    assert(r.built);
    assert(r.compiled_);
    assert(r.diagnostics.empty());

    assert(r.compiled.nodes.size() == 13);
    assert(r.compiled.controlLinks.size() == 1);      // the gate sidechain
    assert(r.compiled.outputNode >= 0);

    const auto position = [&](std::string_view id) {
        for (std::size_t i = 0; i < r.compiled.nodes.size(); ++i)
            if (vocab::shortName(r.compiled.nodes[i].id) == id)
                return static_cast<int>(i);
        return -1;
    };

    // The feedback loop is cut at the unit delay, so everything downstream of
    // the summing node is still ordered after it.
    assert(position("loop") < position("sat1"));
    assert(position("sat1") < position("svf"));
    assert(position("svf")  < position("mix"));
    assert(position("mix")  < position("out"));

    // The sidechain arc carries its depth.
    assert(r.compiled.controlLinks[0].depth == 1.0);
    assert(r.compiled.controlLinks[0].toPort == "amount");

    // Both filter taps are used, from two separate instances.
    int lpTaps = 0, hpTaps = 0;
    for (const auto& link : r.compiled.audioLinks)
    {
        if (link.fromPort == "lp") ++lpTaps;
        if (link.fromPort == "hp") ++hpTaps;
    }
    assert(lpTaps == 2);   // forward to mix, and into the feedback gain
    assert(hpTaps == 1);

    // Parameter bindings resolve to real control inputs.
    assert(r.model.params().size() == 7);
    for (const auto& binding : r.model.params())
    {
        const auto* target = r.model.findElement(binding.targetNode);
        assert(target != nullptr);
        assert(target->type->findProperty(binding.propertySymbol) != nullptr);
        assert(binding.slot >= 0 && binding.slot < 64);
    }
}

}  // namespace

int main()
{
    testSimpleChainCompiles();
    testDefaultsComeFromTheOntology();
    testFeedbackThroughUnitDelayIsAccepted();
    testFeedbackWithoutUnitDelayIsRejected();
    testUnknownPortIsNamed();
    testDirectionAndRateAreChecked();
    testSilentFanInIsRejected();
    testAbstractAndUnknownClassesAreRejected();
    testOutputCardinality();
    testDuplicateAndDanglingArcs();
    testArcDeclaredButNotClaimed();
    testSkreamCompiles();

    std::puts("CircuitCompilerTest PASSED");
    return 0;
}

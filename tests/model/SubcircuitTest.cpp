// tests/model/SubcircuitTest.cpp
//
// A val:Subcircuit is expanded by the model, so every assertion here is about
// what elements(), arcs() and params() look like afterwards. The compiler runs
// too: an expansion that produced an unrunnable circuit would be no use.

#include "valis/CircuitCompiler.h"
#include "valis/CircuitModel.h"
#include "valis/Ontology.h"
#include "valis/Subcircuit.h"
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
        o.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
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
    bool built      = false;
    bool compiledOk = false;

    bool hasDiagnosticContaining(std::string_view fragment) const
    {
        return std::any_of(diagnostics.begin(), diagnostics.end(),
                           [&](const Diagnostic& d)
                           { return d.message.find(fragment) != std::string::npos; });
    }

    const ElementInstance* element(std::string_view id) const
    {
        const auto& list = model.elements();
        const auto it = std::find_if(list.begin(), list.end(),
                                     [&](const ElementInstance& e) { return e.id == id; });
        return it != list.end() ? &*it : nullptr;
    }

    bool hasArc(std::string_view fromNode, std::string_view fromPort,
                std::string_view toNode, std::string_view toPort) const
    {
        return std::any_of(model.arcs().begin(), model.arcs().end(),
                           [&](const Arc& a)
                           {
                               return a.fromNode == fromNode && a.fromPort == fromPort
                                   && a.toNode == toNode && a.toPort == toPort;
                           });
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
        result.compiledOk = compiler.compile(result.model, shippedOntology(),
                                             result.compiled, result.diagnostics);
    }
    return result;
}

const char* kPrefixes = R"(
@prefix val:  <http://purl.org/stuff/valis/> .
@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix :     <urn:valis:t#> .
)";

std::string doc(const std::string& body)
{
    return std::string(kPrefixes) + body;
}

/// One gain stage behind a subcircuit face, used by most cases below.
const char* kStage = R"(
:Stage a val:Subcircuit ;
    rdfs:label "Stage" ;
    lv2:port [ a lv2:InputPort , lv2:AudioPort ; lv2:symbol "in" ;
               val:node :sg ; val:port "in" ] ,
             [ a lv2:OutputPort , lv2:AudioPort ; lv2:symbol "out" ;
               val:node :sg ; val:port "out" ] ,
             [ a lv2:InputPort , lv2:ControlPort ; lv2:symbol "gain" ;
               lv2:default -6.0 ; lv2:minimum -60.0 ; lv2:maximum 12.0 ;
               val:node :sg ; val:port "gain" ] ;
    val:element :sg ;
    val:arc :none .
:sg a val:Gain ; val:gain -24.0 .
)";

// -- expansion --------------------------------------------------------------

void testInstanceExpandsToInnerElements()
{
    auto r = run(doc(std::string(kStage) + R"(
:c a val:Circuit ; val:element :in , :left , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:left a :Stage .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :left ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :left ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    if (! r.compiledOk) r.dump();
    assert(r.built && r.compiledOk);

    // The instance itself is gone; the element behind it carries its name.
    assert(r.element("urn:valis:t#left") == nullptr);

    const auto* inner = r.element("urn:valis:t#left/sg");
    assert(inner != nullptr);
    assert(inner->typeIri == vocab::valTerm("Gain"));

    // Provenance is kept so the Circuit view can draw the instance closed.
    assert(inner->instance == "urn:valis:t#left");
    assert(inner->instanceType == "urn:valis:t#Stage");

    // Both arcs now reach the inner element, not the vanished instance.
    assert(r.hasArc("urn:valis:t#in", "out", "urn:valis:t#left/sg", "in"));
    assert(r.hasArc("urn:valis:t#left/sg", "out", "urn:valis:t#out", "in"));
}

void testTwoInstancesDoNotCollide()
{
    auto r = run(doc(std::string(kStage) + R"(
:c a val:Circuit ; val:element :in , :left , :right , :mix , :out ;
   val:arc :a1 , :a2 , :a3 , :a4 , :a5 .
:in a val:Input .
:left  a :Stage ; val:gain -3.0 .
:right a :Stage ; val:gain -9.0 .
:mix a val:Mixer .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :left ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :right ; val:port "in" ] .
:a3 a val:Arc ; val:from [ val:node :left ; val:port "out" ] ;
                val:to   [ val:node :mix ; val:port "in" ] .
:a4 a val:Arc ; val:from [ val:node :right ; val:port "out" ] ;
                val:to   [ val:node :mix ; val:port "in" ] .
:a5 a val:Arc ; val:from [ val:node :mix ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    if (! r.compiledOk) r.dump();
    assert(r.built && r.compiledOk);

    const auto* left  = r.element("urn:valis:t#left/sg");
    const auto* right = r.element("urn:valis:t#right/sg");
    assert(left != nullptr && right != nullptr);

    // One definition, two independent copies, each with its own value.
    assert(left->valueOf("gain") == -3.0);
    assert(right->valueOf("gain") == -9.0);
}

void testPortDefaultReplacesInnerValueAndInstanceOverridesIt()
{
    auto r = run(doc(std::string(kStage) + R"(
:c a val:Circuit ; val:element :in , :plain , :set , :mix , :out ;
   val:arc :a1 , :a2 , :a3 , :a4 , :a5 .
:in a val:Input .
:plain a :Stage .
:set   a :Stage ; val:gain 6.0 .
:mix a val:Mixer .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :plain ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :set ; val:port "in" ] .
:a3 a val:Arc ; val:from [ val:node :plain ; val:port "out" ] ;
                val:to   [ val:node :mix ; val:port "in" ] .
:a4 a val:Arc ; val:from [ val:node :set ; val:port "out" ] ;
                val:to   [ val:node :mix ; val:port "in" ] .
:a5 a val:Arc ; val:from [ val:node :mix ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    if (! r.compiledOk) r.dump();
    assert(r.built && r.compiledOk);

    // The port's lv2:default is the face the subcircuit presents, so it wins
    // over the -24 the inner element declares.
    assert(r.element("urn:valis:t#plain/sg")->valueOf("gain") == -6.0);

    // The instance's own value sits on top of that.
    assert(r.element("urn:valis:t#set/sg")->valueOf("gain") == 6.0);
}

void testParamBindingFollowsExposedPort()
{
    auto r = run(doc(std::string(kStage) + R"(
:c a val:Circuit ; val:element :in , :left , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:left a :Stage .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :left ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :left ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
:p0 a val:Param ; val:slot 0 ; val:target :left ; val:property val:gain ;
    lv2:name "Left gain" .
)"));

    if (! r.compiledOk) r.dump();
    assert(r.built && r.compiledOk);
    assert(r.model.params().size() == 1);

    // The knob drives the element behind the exposed port.
    assert(r.model.params()[0].targetNode == "urn:valis:t#left/sg");
    assert(r.model.params()[0].propertySymbol == "gain");
}

void testNestedSubcircuitExpands()
{
    auto r = run(doc(std::string(kStage) + R"(
:Pair a val:Subcircuit ;
    lv2:port [ a lv2:InputPort , lv2:AudioPort ; lv2:symbol "in" ;
               val:node :first ; val:port "in" ] ,
             [ a lv2:OutputPort , lv2:AudioPort ; lv2:symbol "out" ;
               val:node :second ; val:port "out" ] ;
    val:element :first , :second ;
    val:arc :inner .
:first  a :Stage .
:second a :Stage .
:inner a val:Arc ; val:from [ val:node :first ; val:port "out" ] ;
                   val:to   [ val:node :second ; val:port "in" ] .

:c a val:Circuit ; val:element :in , :pair , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:pair a :Pair .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :pair ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :pair ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    if (! r.compiledOk) r.dump();
    assert(r.built && r.compiledOk);

    // Two levels of renaming compose.
    assert(r.element("urn:valis:t#pair/first/sg") != nullptr);
    assert(r.element("urn:valis:t#pair/second/sg") != nullptr);

    // An outer arc reaching a nested instance port resolves all the way down.
    assert(r.hasArc("urn:valis:t#in", "out", "urn:valis:t#pair/first/sg", "in"));
    assert(r.hasArc("urn:valis:t#pair/second/sg", "out", "urn:valis:t#out", "in"));

    // Including the arc declared inside the definition.
    assert(r.hasArc("urn:valis:t#pair/first/sg", "out", "urn:valis:t#pair/second/sg", "in"));
}

void testSubcircuitArcIsNotReportedAsUnclaimed()
{
    auto r = run(doc(std::string(kStage) + R"(
:Pair a val:Subcircuit ;
    lv2:port [ a lv2:InputPort , lv2:AudioPort ; lv2:symbol "in" ;
               val:node :first ; val:port "in" ] ,
             [ a lv2:OutputPort , lv2:AudioPort ; lv2:symbol "out" ;
               val:node :second ; val:port "out" ] ;
    val:element :first , :second ;
    val:arc :inner .
:first  a :Stage .
:second a :Stage .
:inner a val:Arc ; val:from [ val:node :first ; val:port "out" ] ;
                   val:to   [ val:node :second ; val:port "in" ] .

:c a val:Circuit ; val:element :in , :pair , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:pair a :Pair .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :pair ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :pair ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    assert(r.built);
    assert(! r.hasDiagnosticContaining("declared but not listed"));
}

// -- voices -----------------------------------------------------------------

void testVoicesStampOutTheDefinitionAndSumIt()
{
    auto r = run(doc(std::string(kStage) + R"(
:c a val:Circuit ; val:element :in , :poly , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:poly a :Stage ; val:voices 3 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :poly ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :poly ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    if (! r.compiledOk) r.dump();
    assert(r.built && r.compiledOk);

    // One copy per voice, each carrying its voice index.
    for (int voice = 0; voice < 3; ++voice)
    {
        const auto id = "urn:valis:t#poly/" + std::to_string(voice) + "/sg";
        const auto* element = r.element(id);
        assert(element != nullptr);
        assert(element->voice == voice);
    }
    assert(r.element("urn:valis:t#poly/3/sg") == nullptr);

    // The expansion adds a mixer to sum them, so what is downstream sees one
    // signal and the compiler's rule that only val:Mixer sums still holds.
    const auto* mixer = r.element("urn:valis:t#poly/sum/out");
    assert(mixer != nullptr);
    assert(mixer->typeIri == vocab::valTerm("Mixer"));
    assert(mixer->voice == -1);

    // Every voice reaches the mixer, and the mixer reaches the output.
    for (int voice = 0; voice < 3; ++voice)
        assert(r.hasArc("urn:valis:t#poly/" + std::to_string(voice) + "/sg", "out",
                        "urn:valis:t#poly/sum/out", "in"));

    assert(r.hasArc("urn:valis:t#poly/sum/out", "out", "urn:valis:t#out", "in"));

    // An input reaches every voice rather than only the first.
    for (int voice = 0; voice < 3; ++voice)
        assert(r.hasArc("urn:valis:t#in", "out",
                        "urn:valis:t#poly/" + std::to_string(voice) + "/sg", "in"));
}

void testVoicesShareTheirDeclaredValues()
{
    auto r = run(doc(std::string(kStage) + R"(
:c a val:Circuit ; val:element :in , :poly , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:poly a :Stage ; val:voices 2 ; val:gain 3.0 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :poly ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :poly ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
:p0 a val:Param ; val:slot 0 ; val:target :poly ; val:property val:gain .
)"));

    if (! r.compiledOk) r.dump();
    assert(r.built && r.compiledOk);

    // The value set on the instance reaches every copy.
    assert(r.element("urn:valis:t#poly/0/sg")->valueOf("gain") == 3.0);
    assert(r.element("urn:valis:t#poly/1/sg")->valueOf("gain") == 3.0);

    // And one knob drives all of them, or turning it would let the voices drift
    // apart from each other.
    assert(r.model.params().size() == 1);
    const auto& binding = r.model.params().front();
    assert(binding.targetNode == "urn:valis:t#poly/0/sg");
    assert(binding.alsoTargets.size() == 1);
    assert(binding.alsoTargets.front().first == "urn:valis:t#poly/1/sg");
    assert(binding.alsoTargets.front().second == "gain");
}

void testVoiceCountIsBounded()
{
    const auto attempt = [](const char* count)
    {
        return run(doc(std::string(kStage) + R"(
:c a val:Circuit ; val:element :in , :poly , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:poly a :Stage ; val:voices )" + std::string(count) + R"( .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :poly ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :poly ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));
    };

    assert(attempt("0").hasDiagnosticContaining("val:voices must be between"));
    assert(attempt("17").hasDiagnosticContaining("val:voices must be between"));
    assert(attempt("16").built);
}

void testPolyphonicControlOutputIsRejected()
{
    // There is one value per voice and no honest way to choose between them, so
    // exposing a control output from a pool has to be an error rather than a
    // silent pick of the first voice.
    auto r = run(doc(R"(
:Detect a val:Subcircuit ;
    lv2:port [ a lv2:InputPort , lv2:AudioPort ; lv2:symbol "in" ;
               val:node :follow ; val:port "in" ] ,
             [ a lv2:OutputPort , lv2:ControlPort ; lv2:symbol "level" ;
               val:node :follow ; val:port "out" ] ;
    val:element :follow .
:follow a val:EnvelopeFollower .

:c a val:Circuit ; val:element :in , :poly , :out ; val:arc :a1 .
:in a val:Input .
:poly a :Detect ; val:voices 2 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    assert(r.hasDiagnosticContaining("one value per voice"));
}

// -- failure ----------------------------------------------------------------

void testPortWithoutMappingIsReported()
{
    auto r = run(doc(R"(
:Bad a val:Subcircuit ;
    lv2:port [ a lv2:InputPort , lv2:AudioPort ; lv2:symbol "in" ] ;
    val:element :sg .
:sg a val:Gain .

:c a val:Circuit ; val:element :in , :out ; val:arc :a1 .
:in a val:Input .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    assert(r.hasDiagnosticContaining("naming what it exposes"));
}

void testPortExposingForeignElementIsReported()
{
    auto r = run(doc(R"(
:Bad a val:Subcircuit ;
    lv2:port [ a lv2:InputPort , lv2:AudioPort ; lv2:symbol "in" ;
               val:node :elsewhere ; val:port "in" ] ;
    val:element :sg .
:sg a val:Gain .
:elsewhere a val:Gain .

:c a val:Circuit ; val:element :in , :out ; val:arc :a1 .
:in a val:Input .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    assert(r.hasDiagnosticContaining("which is not one of its val:element"));
}

void testSelfInstantiatingSubcircuitIsBounded()
{
    // A definition that contains an instance of itself would recurse forever.
    // The depth limit turns it into a diagnostic instead.
    auto r = run(doc(R"(
:Loop a val:Subcircuit ;
    lv2:port [ a lv2:InputPort , lv2:AudioPort ; lv2:symbol "in" ;
               val:node :inner ; val:port "in" ] ,
             [ a lv2:OutputPort , lv2:AudioPort ; lv2:symbol "out" ;
               val:node :inner ; val:port "out" ] ;
    val:element :inner .
:inner a :Loop .

:c a val:Circuit ; val:element :in , :loop , :out ; val:arc :a1 , :a2 .
:in a val:Input .
:loop a :Loop .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ;
                val:to   [ val:node :loop ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :loop ; val:port "out" ] ;
                val:to   [ val:node :out ; val:port "in" ] .
)"));

    assert(r.hasDiagnosticContaining("instantiates itself"));
}

void testSubcircuitClassIsNotConstructibleAsAnElement()
{
    // val:Subcircuit carries no val:implementation, so the ontology must refuse
    // to resolve it as an element class.
    assert(shippedOntology().find(vocab::val::Subcircuit) == nullptr);
}

}  // namespace

int main()
{
    testInstanceExpandsToInnerElements();
    testTwoInstancesDoNotCollide();
    testPortDefaultReplacesInnerValueAndInstanceOverridesIt();
    testParamBindingFollowsExposedPort();
    testNestedSubcircuitExpands();
    testSubcircuitArcIsNotReportedAsUnclaimed();

    testVoicesStampOutTheDefinitionAndSumIt();
    testVoicesShareTheirDeclaredValues();
    testVoiceCountIsBounded();
    testPolyphonicControlOutputIsRejected();

    testPortWithoutMappingIsReported();
    testPortExposingForeignElementIsReported();
    testSelfInstantiatingSubcircuitIsBounded();
    testSubcircuitClassIsNotConstructibleAsAnElement();

    std::puts("SubcircuitTest PASSED");
    return 0;
}

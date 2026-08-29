// tests/rdf/TurtleStoreTest.cpp

#include "valis/TurtleStore.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;
using namespace valis::rdf;

namespace {

const char* kCircuit = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix :    <urn:valis:circuit#> .

:main a val:Circuit ;
      val:element :osc , :vcf ;
      val:arc :a1 .

:osc a val:Oscillator ; val:frequency 440.0 .
:vcf a val:Ladder ; val:cutoff 800.0 ; val:resonance 0.7 ; val:linear false .

:a1 a val:Arc ;
    val:from [ val:node :osc ; val:port "out" ] ;
    val:to   [ val:node :vcf ; val:port "in"  ] .
)";

void testParseAndQuery()
{
    TurtleStore store;
    std::vector<ParseError> errors;
    assert(store.parse(kCircuit, "urn:valis:test", errors));
    assert(errors.empty());
    assert(store.size() > 0);

    // Typed subjects.
    const auto circuits = store.subjectsOfType(vocab::val::Circuit);
    assert(circuits.size() == 1);
    assert(circuits[0].isUri());
    assert(circuits[0].string() == "urn:valis:circuit#main");

    // Multi-valued property.
    const auto elements = store.objects(circuits[0], vocab::val::element);
    assert(elements.size() == 2);

    // Literal typing. Turtle 440.0 is an xsd:double.
    auto vcf = store.uri("urn:valis:circuit#vcf");
    auto cutoff = store.object(vcf, vocab::valTerm("cutoff"));
    assert(cutoff.isLiteral());
    assert(cutoff.asDouble().has_value());
    assert(*cutoff.asDouble() == 800.0);

    auto linear = store.object(vcf, vocab::val::linear);
    assert(linear.asBool().has_value() && *linear.asBool() == false);

    // Arc endpoints are blank nodes carrying node + port.
    const auto arcs = store.subjectsOfType(vocab::val::Arc);
    assert(arcs.size() == 1);
    auto from = store.object(arcs[0], vocab::val::from);
    assert(from.isBlank());
    auto fromNode = store.object(from, vocab::val::node);
    assert(fromNode.string() == "urn:valis:circuit#osc");
    auto fromPort = store.object(from, vocab::val::port);
    assert(fromPort.string() == "out");

    // Term comparison uses sord_node_equals, not pointer identity: a node built
    // independently must compare equal to one that came out of a query.
    assert(fromNode == store.uri("urn:valis:circuit#osc"));
    assert(! (fromNode == store.uri("urn:valis:circuit#vcf")));

    // A property that is not there yields an invalid Node, not a crash.
    assert(! store.object(vcf, vocab::valTerm("nosuch")));
}

void testErrorsCarryPosition()
{
    TurtleStore store;
    std::vector<ParseError> errors;

    // Line 3 is missing its terminating '.'.
    const char* broken =
        "@prefix val: <http://purl.org/stuff/valis/> .\n"
        "@prefix : <urn:valis:circuit#> .\n"
        ":osc a val:Oscillator\n"
        ":vcf a val:Ladder .\n";

    const bool ok = store.parse(broken, "urn:valis:test", errors);
    assert(! ok);
    assert(! errors.empty());

    // The editor gutter needs a real position, not 0:0.
    assert(errors[0].line > 0);
    assert(! errors[0].message.empty());
    // And no trailing newline, which would break a single-line gutter label.
    assert(errors[0].message.back() != '\n');

    std::printf("  parse error reported as: %s\n", errors[0].toString().c_str());
}

void testRoundTrip()
{
    TurtleStore first;
    std::vector<ParseError> errors;
    assert(first.parse(kCircuit, "urn:valis:test", errors));

    first.registerPrefix("val", vocab::VAL);
    const std::string out = first.serialise();
    assert(! out.empty());

    // Re-parsing the output must give a store of the same size. Comments and
    // layout are not preserved; the triples are.
    TurtleStore second;
    std::vector<ParseError> errors2;
    assert(second.parse(out, "urn:valis:test", errors2));
    assert(errors2.empty());
    assert(second.size() == first.size());

    const auto circuits = second.subjectsOfType(vocab::val::Circuit);
    assert(circuits.size() == 1);
}

void testMutation()
{
    TurtleStore store;
    std::vector<ParseError> errors;
    assert(store.parse(kCircuit, "urn:valis:test", errors));

    const auto before = store.size();
    auto s = store.uri("urn:valis:circuit#gain");
    auto p = store.uri(vocab::rdf::type);
    auto o = store.uri(vocab::valTerm("Gain"));

    store.add(s, p, o);
    assert(store.size() == before + 1);
    assert(store.contains(s, vocab::rdf::type, o));
    assert(store.subjectsOfType(vocab::valTerm("Gain")).size() == 1);

    store.remove(s, p, o);
    assert(store.size() == before);
    assert(! store.contains(s, vocab::rdf::type, o));
}

/// Every vocabulary the project draws on must parse. This catches a bad edit to
/// vocabs/valis.ttl immediately, and pins the third-party copies we vendored.
void testVocabulariesParse()
{
    const char* files[] = {
        VALIS_VOCABS_DIR "/valis.ttl",
        VALIS_EXAMPLES_DIR "/skream.ttl",
        VALIS_EXAMPLES_DIR "/basic.ttl",
        VALIS_ROOT_DIR "/profile.ttl",
        VALIS_VOCABS_DIR "/lv2/lv2core.ttl",
        VALIS_VOCABS_DIR "/lv2/units.ttl",
        VALIS_VOCABS_DIR "/lv2/atom.ttl",
        VALIS_VOCABS_DIR "/lv2/patch.ttl",
        VALIS_VOCABS_DIR "/lv2/parameters.ttl",
        VALIS_VOCABS_DIR "/lv2/port-groups.ttl",
        VALIS_VOCABS_DIR "/w3c/rdf.ttl",
        VALIS_VOCABS_DIR "/w3c/rdfs.ttl",
        VALIS_VOCABS_DIR "/w3c/owl.ttl",
    };

    for (const char* path : files)
    {
        TurtleStore store;
        std::vector<ParseError> errors;
        const bool ok = store.parseFile(path, errors);
        if (! ok)
            std::printf("  FAILED %s: %s\n", path,
                        errors.empty() ? "(no message)" : errors[0].toString().c_str());
        assert(ok);
        assert(store.size() > 0);
    }
}

/// The ontology is loaded at runtime, so its shape is a contract. Every class
/// carrying val:implementation must be instantiable, and the registry test in
/// M3 will assert the reverse direction.
void testOntologyShape()
{
    TurtleStore store;
    std::vector<ParseError> errors;
    assert(store.parseFile(VALIS_VOCABS_DIR "/valis.ttl", errors));

    const auto implemented = store.subjects(vocab::val::implementation,
                                            store.literal("Ladder"));
    assert(implemented.size() == 1);
    assert(implemented[0].string() == vocab::valTerm("Ladder"));

    // Ladder is a nonlinear Filter: the split is memory vs. memoryless, and
    // linearity is a separate property.
    auto ladder = store.uri(vocab::valTerm("Ladder"));
    assert(store.contains(ladder, vocab::rdfs::subClassOf, store.uri(vocab::val::Filter)));
    auto lin = store.object(ladder, vocab::val::linear);
    assert(lin.asBool().has_value() && *lin.asBool() == false);

    // The fine-grained device models carry physical parameters, so a Diode is a
    // diode rather than a relabelled saturator.
    auto diode = store.uri(vocab::valTerm("Diode"));
    assert(store.contains(diode, vocab::rdfs::subClassOf, store.uri(vocab::val::Transfer)));

    int controlPorts = 0;
    bool sawSaturationCurrent = false;
    for (const auto& port : store.objects(diode, vocab::lv2::port))
    {
        if (store.contains(port, vocab::rdf::type, store.uri(vocab::lv2::ControlPort)))
            ++controlPorts;

        if (auto sym = store.object(port, vocab::lv2::symbol);
            sym && sym.string() == "saturationCurrent")
        {
            sawSaturationCurrent = true;
            auto def = store.object(port, vocab::lv2::defaultV);
            assert(def.asDouble().has_value());
        }
    }
    assert(sawSaturationCurrent);
    assert(controlPorts == 3);   // Is, n, Vt

    // val:NonLinear is related to val:Transfer, not merged with it.
    auto nonLinear = store.uri(vocab::val::NonLinear);
    assert(store.contains(nonLinear, vocab::owl::equivalentClass,
                          store.uri(vocab::val::Transfer)));
}

/// examples/skream.ttl is the project's proof of concept (docs/skream.md). Its
/// shape is asserted here so a careless edit to the circuit or the ontology is
/// caught before it reaches the engine.
void testSkreamCircuit()
{
    TurtleStore store;
    std::vector<ParseError> errors;
    assert(store.parseFile(VALIS_EXAMPLES_DIR "/skream.ttl", errors));
    assert(errors.empty());

    const auto circuits = store.subjectsOfType(vocab::val::Circuit);
    assert(circuits.size() == 1);

    // Every element and arc named by the circuit must actually be declared, and
    // every declared arc must be claimed by the circuit.
    const auto declaredArcs = store.subjectsOfType(vocab::val::Arc);
    const auto claimedArcs  = store.objects(circuits[0], vocab::val::arc);
    assert(declaredArcs.size() == claimedArcs.size());

    for (const auto& element : store.objects(circuits[0], vocab::val::element))
        assert(store.object(element, vocab::rdf::type));

    // Both endpoints of every arc must name a node and a port.
    int controlArcs = 0;
    for (const auto& arc : declaredArcs)
    {
        for (const auto* end : {&vocab::val::from, &vocab::val::to})
        {
            auto endpoint = store.object(arc, *end);
            assert(endpoint.isBlank());
            assert(store.object(endpoint, vocab::val::node).isUri());
            assert(! store.object(endpoint, vocab::val::port).string().empty());
        }

        if (store.object(arc, vocab::valTerm("depth")))
            ++controlArcs;
    }
    assert(controlArcs == 1);   // the gate sidechain

    // The feedback loop must pass through a unit delay, or the compiler will
    // reject the circuit in M2.
    assert(store.subjectsOfType(vocab::valTerm("UnitDelay")).size() == 1);

    // One filter feeds the forward path from lp while a second feeds the
    // return path from hp: named multi-output ports, not a numeric mode.
    bool sawLp = false, sawHp = false;
    for (const auto& arc : declaredArcs)
    {
        auto port = store.object(store.object(arc, vocab::val::from), vocab::val::port);
        if (port.string() == "lp") sawLp = true;
        if (port.string() == "hp") sawHp = true;
    }
    assert(sawLp && sawHp);

    // Parameter slots must be unique.
    const auto params = store.subjectsOfType(vocab::val::Param);
    assert(params.size() >= 6);
    std::vector<int64_t> slots;
    for (const auto& param : params)
    {
        auto slot = store.object(param, vocab::val::slot);
        assert(slot.asInt().has_value());
        assert(*slot.asInt() >= 0 && *slot.asInt() < 64);
        slots.push_back(*slot.asInt());
    }
    std::sort(slots.begin(), slots.end());
    assert(std::adjacent_find(slots.begin(), slots.end()) == slots.end());
}

}  // namespace

int main()
{
    testParseAndQuery();
    testErrorsCarryPosition();
    testRoundTrip();
    testMutation();
    testVocabulariesParse();
    testOntologyShape();
    testSkreamCircuit();

    std::puts("TurtleStoreTest PASSED");
    return 0;
}

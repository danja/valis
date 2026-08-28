// tests/model/OntologyTest.cpp

#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

Ontology loadShipped()
{
    Ontology ontology;
    std::vector<std::string> errors;

    // Units first: symbols are resolved as ports are read.
    ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);

    if (! ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors))
        for (const auto& e : errors)
            std::printf("  ontology error: %s\n", e.c_str());

    assert(errors.empty());
    return ontology;
}

void testLoadsEveryImplementableClass()
{
    const auto ontology = loadShipped();
    assert(ontology.size() > 0);

    // Abstract classes carry no val:implementation and must not be constructible.
    assert(ontology.find(vocab::val::Element) == nullptr);
    assert(ontology.find(vocab::val::Filter) == nullptr);
    assert(ontology.find(vocab::val::Transfer) == nullptr);
    assert(ontology.find(vocab::valTerm("Network")) == nullptr);

    // val:NonLinear is an alias of val:Transfer, so both resolve alike.
    assert(ontology.find(vocab::val::NonLinear) == ontology.find(vocab::val::Transfer));

    assert(ontology.find("http://example.org/NotAThing") == nullptr);
}

void testImplementationKeysAreUnique()
{
    const auto ontology = loadShipped();
    auto keys = ontology.implementationKeys();

    assert(keys.size() == ontology.size());
    assert(! keys.empty());

    // Two classes sharing a registry key would make construction ambiguous.
    assert(std::adjacent_find(keys.begin(), keys.end()) == keys.end());

    for (const auto& key : keys)
        assert(! key.empty());
}

void testLadder()
{
    const auto ontology = loadShipped();
    const auto* ladder = ontology.find(vocab::valTerm("Ladder"));
    assert(ladder != nullptr);
    assert(ladder->implementation == "Ladder");

    // Has memory and is nonlinear: the Filter/Transfer split is about memory,
    // linearity is a separate property.
    assert(! ladder->linear);

    assert(ladder->findPort("in") != nullptr);
    assert(ladder->findPort("out") != nullptr);
    assert(ladder->findPort("nosuch") == nullptr);

    assert(ladder->findPort("in")->input);
    assert(ladder->findPort("in")->isAudio());
    assert(! ladder->findPort("out")->input);

    const auto* cutoff = ladder->findProperty("cutoff");
    assert(cutoff != nullptr);
    assert(cutoff->control && cutoff->input);
    assert(cutoff->defaultValue == 1000.0);
    assert(cutoff->minimum == 20.0);
    assert(cutoff->maximum == 20000.0);
    // The symbol comes from the vendored LV2 units vocabulary, not from the
    // local name of the IRI - so it reads "Hz", not "hz".
    assert(cutoff->unitSymbol == "Hz");
    assert(cutoff->name == "Cutoff");

    // An audio port is not a settable property.
    assert(ladder->findProperty("in") == nullptr);

    assert(ladder->portsMatching(true, false).size() == 1);   // audio in
    assert(ladder->portsMatching(false, false).size() == 1);  // audio out
    assert(ladder->portsMatching(true, true).size() == 3);    // cutoff, resonance, drive
}

/// Scream taps one filter at lp and another at hp, so the SVF must expose all
/// three responses as named ports rather than switching on a numeric mode.
void testStateVariableHasNamedOutputs()
{
    const auto ontology = loadShipped();
    const auto* svf = ontology.find(vocab::valTerm("StateVariable"));
    assert(svf != nullptr);

    for (const char* symbol : {"lp", "bp", "hp"})
    {
        const auto* port = svf->findPort(symbol);
        assert(port != nullptr);
        assert(! port->input);
        assert(port->isAudio());
    }

    assert(svf->findPort("mode") == nullptr);   // replaced by the named outputs
    assert(svf->portsMatching(false, false).size() == 3);
}

/// The fine-grained device models carry real physical parameters.
void testDiodeIsADiode()
{
    const auto ontology = loadShipped();
    const auto* diode = ontology.find(vocab::valTerm("Diode"));
    assert(diode != nullptr);
    assert(! diode->linear);

    const auto* is = diode->findProperty("saturationCurrent");
    assert(is != nullptr);
    assert(is->defaultValue > 0.0 && is->defaultValue < 1e-6);   // 1N4148, ~2.5 nA
    assert(is->unitSymbol == "A");

    const auto* n = diode->findProperty("emissionCoefficient");
    assert(n != nullptr && n->minimum >= 1.0 && n->maximum <= 2.0);

    const auto* vt = diode->findProperty("thermalVoltage");
    assert(vt != nullptr && vt->unitSymbol == "V");

    // DiodePair shares the Shockley parameters and adds a series resistance.
    const auto* pair = ontology.find(vocab::valTerm("DiodePair"));
    assert(pair != nullptr);
    assert(pair->findProperty("saturationCurrent") != nullptr);
    assert(pair->findProperty("seriesResistance") != nullptr);
}

void testAntiAliasingIsDeclared()
{
    const auto ontology = loadShipped();

    const auto* tanh = ontology.find(vocab::valTerm("Tanh"));
    assert(tanh != nullptr);
    assert(tanh->antialiasing == vocab::valTerm("ADAA2"));

    // A linear element has no reason to declare one.
    const auto* gain = ontology.find(vocab::valTerm("Gain"));
    assert(gain != nullptr && gain->linear);
    assert(gain->antialiasing.empty());
}

/// Every element type the Skream circuit names must exist, with the ports the
/// circuit's arcs address. This is the acceptance demo's contract.
void testSkreamTypesResolve()
{
    const auto ontology = loadShipped();

    struct { const char* type; const char* port; } required[] = {
        {"Input", "out"},        {"Output", "in"},
        {"Gain", "in"},          {"Gain", "out"},
        {"Mixer", "in"},         {"Mixer", "out"},
        {"Tanh", "in"},          {"Tanh", "out"},
        {"StateVariable", "lp"}, {"StateVariable", "hp"},
        {"Expander", "in"},      {"Expander", "amount"},
        {"EnvelopeFollower", "in"}, {"EnvelopeFollower", "out"},
        {"UnitDelay", "in"},     {"UnitDelay", "out"},
        {"DryWet", "dry"},       {"DryWet", "wet"},  {"DryWet", "out"},
    };

    for (const auto& [type, port] : required)
    {
        const auto* element = ontology.find(vocab::valTerm(type));
        if (element == nullptr)
            std::printf("  missing type: val:%s\n", type);
        assert(element != nullptr);

        if (element->findPort(port) == nullptr)
            std::printf("  val:%s has no port '%s'\n", type, port);
        assert(element->findPort(port) != nullptr);
    }

    // The gate's sidechain arc runs from a control output to a control input.
    const auto* env = ontology.find(vocab::valTerm("EnvelopeFollower"));
    assert(env->findPort("out")->control);
    const auto* gate = ontology.find(vocab::valTerm("Expander"));
    assert(gate->findPort("amount")->control && gate->findPort("amount")->input);
    assert(gate->findPort("in")->isAudio());
}

void testRejectsMalformedOntology()
{
    // A class that says it is implementable but declares no ports.
    Ontology ontology;
    std::vector<std::string> errors;
    const bool ok = ontology.loadTurtle(R"(
        @prefix val:  <http://purl.org/stuff/valis/> .
        @prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
        val:Broken a rdfs:Class ; val:implementation "Broken" .
    )", errors);

    assert(! ok);
    assert(! errors.empty());

    // And Turtle that will not parse at all.
    Ontology bad;
    std::vector<std::string> parseErrors;
    assert(! bad.loadTurtle("@prefix val: <http://purl.org/stuff/valis/>\nval:X a", parseErrors));
    assert(! parseErrors.empty());
}

}  // namespace

int main()
{
    testLoadsEveryImplementableClass();
    testImplementationKeysAreUnique();
    testLadder();
    testStateVariableHasNamedOutputs();
    testDiodeIsADiode();
    testAntiAliasingIsDeclared();
    testSkreamTypesResolve();
    testRejectsMalformedOntology();

    const auto ontology = loadShipped();
    std::printf("OntologyTest PASSED (%zu implementable classes)\n", ontology.size());
    return 0;
}

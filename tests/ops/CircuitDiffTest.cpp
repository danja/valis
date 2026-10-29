// tests/ops/CircuitDiffTest.cpp
//
// valis::diffCircuits: what changed between two circuits, in sentences.

#include "valis/CircuitDiff.h"
#include "valis/CircuitModel.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

const Ontology& ontology()
{
    static const Ontology o = [] {
        Ontology loaded;
        std::vector<std::string> errors;
        loaded.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool ok = loaded.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(ok);
        return loaded;
    }();
    return o;
}

std::vector<std::string> diffOf(const std::string& a, const std::string& b)
{
    const std::string prefixes = "@prefix val: <http://purl.org/stuff/valis/> .\n@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
                                 "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n@prefix : <urn:valis:diff#> .\n";
    CircuitModel before, after;
    for (const auto& [text, model] : {std::pair<const std::string*, CircuitModel*>{&a, &before}, {&b, &after}})
    {
        rdf::TurtleStore store;
        std::vector<rdf::ParseError> errors;
        std::vector<Diagnostic> diagnostics;
        const bool parsed = store.parse(prefixes + *text, "urn:valis:diff", errors);
        assert(parsed);
        const bool built = model->build(store, ontology(), diagnostics);
        assert(built);
    }
    return diffCircuits(before, after);
}

bool says(const std::vector<std::string>& changes, const char* fragment)
{
    return std::any_of(changes.begin(), changes.end(), [&](const std::string& c) { return c.find(fragment) != std::string::npos; });
}

const char* kBase = R"(
:c a val:Circuit ; rdfs:label "C" ; val:element :osc , :vcf , :out ; val:arc :a1 , :a2 .
:osc a val:Oscillator ; val:shape 1.0 .
:vcf a val:Ladder ; val:cutoff 800.0 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :osc ; val:port "out" ] ; val:to [ val:node :vcf ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :vcf ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
)";

void testTheSameCircuitHasNoChanges()
{
    assert(diffOf(kBase, kBase).empty());

    // Layout and order are not changes: only what the circuit means.
    const std::string reordered = R"(
:out a val:Output .
:vcf a val:Ladder ;
     val:cutoff 800.0 .
:osc a val:Oscillator ; val:shape 1.0 .
:a2 a val:Arc ; val:from [ val:node :vcf ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
:a1 a val:Arc ; val:from [ val:node :osc ; val:port "out" ] ; val:to [ val:node :vcf ; val:port "in" ] .
:c a val:Circuit ; rdfs:label "C" ; val:element :out , :vcf , :osc ; val:arc :a2 , :a1 .
)";
    assert(diffOf(kBase, reordered).empty());
}

void testElementsAndValues()
{
    std::string changed = kBase;
    changed.replace(changed.find("val:cutoff 800.0"), 16, "val:cutoff 1200.0 ; val:resonance 0.5");
    auto c = diffOf(kBase, changed);
    assert(says(c, "vcf.cutoff changed from 800 to 1200"));
    assert(says(c, "vcf.resonance set to 0.5"));
    assert(c.size() == 2);

    c = diffOf(changed, kBase);
    assert(says(c, "vcf.cutoff changed from 1200 to 800"));
    assert(says(c, "vcf.resonance no longer set (was 0.5)"));

    std::string extra = kBase;
    extra.replace(extra.find(":osc , :vcf , :out ;"), 20, ":osc , :vcf , :out , :lfo ;");
    extra += ":lfo a val:LFO .\n";
    c = diffOf(kBase, extra);
    assert(c.size() == 1 && says(c, "added lfo (val:LFO)"));
    assert(says(diffOf(extra, kBase), "removed lfo (val:LFO)"));

    std::string swapped = kBase;
    swapped.replace(swapped.find(":vcf a val:Ladder"), 17, ":vcf a val:StateVariable");
    assert(says(diffOf(kBase, swapped), "vcf changed from val:Ladder to val:StateVariable"));
}

void testOptions()
{
    const std::string a = std::string(kBase) + ":seq a val:StepSequencer ; val:pattern \"C3 E3\" .\n";
    std::string withSeq = a;
    withSeq.replace(withSeq.find(":osc , :vcf , :out ;"), 20, ":osc , :vcf , :out , :seq ;");
    std::string b = withSeq;
    b.replace(b.find("C3 E3"), 5, "C3 G3");

    const auto c = diffOf(withSeq, b);
    assert(c.size() == 1);
    assert(says(c, "seq option pattern changed from 'C3 E3' to 'C3 G3'"));
}

void testArcs()
{
    std::string cut = kBase;
    cut.replace(cut.find("val:arc :a1 , :a2"), 17, "val:arc :a2");
    auto c = diffOf(kBase, cut);
    assert(c.size() == 1 && says(c, "disconnected osc.out to vcf.in"));
    assert(says(diffOf(cut, kBase), "connected osc.out to vcf.in"));

    // A control arc says so.
    std::string modulated = kBase;
    modulated.replace(modulated.find(":osc , :vcf , :out ;"), 20, ":osc , :vcf , :out , :lfo ;");
    modulated.replace(modulated.find("val:arc :a1 , :a2"), 17, "val:arc :a1 , :a2 , :c1");
    modulated += ":lfo a val:LFO .\n:c1 a val:ControlArc ; val:from [ val:node :lfo ; val:port \"out\" ] ; val:to [ val:node :vcf ; val:port \"cutoff\" ] .\n";
    c = diffOf(kBase, modulated);
    assert(says(c, "added lfo"));
    assert(says(c, "connected lfo.out to vcf.cutoff (control)"));
}

void testParameters()
{
    const std::string bound = std::string(kBase) +
        ":p0 a val:Param ; val:slot 0 ; val:target :vcf ; val:property val:cutoff ; lv2:name \"Cutoff\" ; lv2:symbol \"cutoff\" .\n";
    auto c = diffOf(kBase, bound);
    assert(c.size() == 1 && says(c, "bound slot 0 (Cutoff, vcf.cutoff)"));
    assert(says(diffOf(bound, kBase), "unbound slot 0"));

    std::string moved = bound;
    moved.replace(moved.find("val:target :vcf ; val:property val:cutoff"), 41, "val:target :vcf ; val:property val:resonance");
    c = diffOf(bound, moved);
    assert(c.size() == 1 && says(c, "is now slot 0 (Cutoff, vcf.resonance)"));
}

}  // namespace

int main()
{
    testTheSameCircuitHasNoChanges();
    testElementsAndValues();
    testOptions();
    testArcs();
    testParameters();

    std::puts("CircuitDiffTest PASSED");
    return 0;
}

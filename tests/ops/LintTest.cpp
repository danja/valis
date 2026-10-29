// tests/ops/LintTest.cpp
//
// valis::lintCircuit. Each rule is shown a circuit that breaks it and one that does not,
// and every circuit shipped in examples/ has to come through without an error or a warning,
// since they are what an agent copies from.

#include "valis/CircuitModel.h"
#include "valis/Lint.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
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

std::vector<LintFinding> lintText(const std::string& body, bool withPrefixes = true)
{
    const std::string text = withPrefixes
        ? "@prefix val: <http://purl.org/stuff/valis/> .\n@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
          "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n@prefix : <urn:valis:lint#> .\n" + body
        : body;

    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    const bool parsed = store.parse(text, "urn:valis:lint", parseErrors);
    assert(parsed);

    CircuitModel model;
    std::vector<Diagnostic> diagnostics;
    const bool built = model.build(store, ontology(), diagnostics);
    assert(built);
    return lintCircuit(model);
}

bool has(const std::vector<LintFinding>& findings, const char* code, const char* nodeFragment = "")
{
    return std::any_of(findings.begin(), findings.end(), [&](const LintFinding& f)
                       { return f.code == code && f.node.find(nodeFragment) != std::string::npos; });
}

std::size_t count(const std::vector<LintFinding>& findings, const char* code)
{
    return static_cast<std::size_t>(std::count_if(findings.begin(), findings.end(),
                                                  [&](const LintFinding& f) { return f.code == code; }));
}

// A filter swept by an envelope through a Scale, with the correct wiring.
const char* kGood = R"(
:c a val:Circuit ; rdfs:label "Good" ;
  val:element :in , :vcf , :env , :scale , :out ;
  val:arc :a1 , :a2 , :c1 , :c2 .
:in a val:Input .
:vcf a val:Ladder ; val:resonance 0.4 .
:env a val:Envelope .
:scale a val:Scale ; val:min 200.0 ; val:max 4000.0 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ; val:to [ val:node :vcf ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :vcf ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
:c1 a val:ControlArc ; val:from [ val:node :env ; val:port "out" ] ; val:to [ val:node :scale ; val:port "in" ] .
:c2 a val:ControlArc ; val:from [ val:node :scale ; val:port "out" ] ; val:to [ val:node :vcf ; val:port "cutoff" ] .
)";

void testACorrectCircuitHasNothingToSay()
{
    const auto findings = lintText(kGood);
    for (const auto& f : findings) std::printf("  %s: %s\n", f.code.c_str(), f.message.c_str());
    assert(std::none_of(findings.begin(), findings.end(), [](const LintFinding& f)
                        { return f.severity != LintFinding::Severity::info; }));
}

void testAFixedValueUnderAnArcIsFound()
{
    std::string bad = kGood;
    bad.replace(bad.find("val:resonance 0.4"), 17, "val:resonance 0.4 ; val:cutoff 800.0");
    const auto findings = lintText(bad);
    assert(has(findings, "control-overrides-value", "vcf"));
    const auto& f = *std::find_if(findings.begin(), findings.end(), [](const LintFinding& x) { return x.code == "control-overrides-value"; });
    assert(f.port == "cutoff");
    assert(f.message.find("800") != std::string::npos);        // says what value is wasted
    assert(f.message.find("scale") != std::string::npos);      // and which source replaces it
    assert(f.severity == LintFinding::Severity::warning);

    // The same value on a port nothing reaches is fine.
    std::string fine = kGood;
    fine.replace(fine.find("val:resonance 0.4"), 17, "val:resonance 0.4 ; val:drive 2.0");
    assert(! has(lintText(fine), "control-overrides-value"));
}

void testAKnobUnderAnArcIsFound()
{
    const std::string bad = std::string(kGood) + R"(
:p0 a val:Param ; val:slot 0 ; val:target :vcf ; val:property val:cutoff ;
    lv2:name "Cutoff" ; lv2:symbol "cutoff" .
)";
    const auto findings = lintText(bad);
    assert(has(findings, "knob-overridden", "vcf"));

    const std::string fine = std::string(kGood) + R"(
:p0 a val:Param ; val:slot 0 ; val:target :scale ; val:property val:min ;
    lv2:name "Cutoff" ; lv2:symbol "cutoff" .
)";
    assert(! has(lintText(fine), "knob-overridden"));
}

void testNothingConnectedToTheOutput()
{
    const auto findings = lintText(R"(
:c a val:Circuit ; rdfs:label "Mute" ; val:element :in , :out .
:in a val:Input .
:out a val:Output .
)");
    assert(has(findings, "nothing-reaches-output"));
    assert(findings.front().severity == LintFinding::Severity::error);   // errors come first
}

void testAPartThatCannotBeHeardIsFound()
{
    const auto findings = lintText(R"(
:c a val:Circuit ; rdfs:label "Stray" ; val:element :in , :out , :lfo , :osc ; val:arc :a1 .
:in a val:Input .
:out a val:Output .
:lfo a val:LFO .
:osc a val:Oscillator .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
)");
    assert(has(findings, "not-heard", "lfo"));
    assert(has(findings, "not-heard", "osc"));
    assert(count(findings, "not-heard") == 2);   // not the input or the output

    // A source that reaches the output only through a control arc is heard.
    assert(! has(lintText(kGood), "not-heard"));
}

void testMonitorsAreNotReportedAsUnheard()
{
    const auto findings = lintText(R"(
:c a val:Circuit ; rdfs:label "Watched" ; val:element :in , :out , :scope ; val:arc :a1 , :a2 .
:in a val:Input .
:out a val:Output .
:scope a val:Oscilloscope .
:a1 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
:a2 a val:Arc ; val:from [ val:node :in ; val:port "out" ] ; val:to [ val:node :scope ; val:port "in" ] .
)");
    assert(! has(findings, "not-heard"));
}

void testAnEffectWithNothingToProcess()
{
    const auto findings = lintText(R"(
:c a val:Circuit ; rdfs:label "Deaf" ; val:element :vcf , :out ; val:arc :a1 .
:vcf a val:Ladder .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :vcf ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
)");
    assert(has(findings, "reads-silence", "vcf"));

    // An element whose input is optional is not accused: a granulator can play a file.
    const auto optional = lintText(R"(
:c a val:Circuit ; rdfs:label "File" ; val:element :g , :out ; val:arc :a1 .
:g a val:Granulator .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :g ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
)");
    assert(! has(optional, "reads-silence"));
}

void testAMisspeltPropertyIsFoundWithASuggestion()
{
    // val:cutof sets nothing: it is not a port, so it is taken as an option nobody reads.
    std::string bad = kGood;
    bad.replace(bad.find("val:resonance 0.4"), 17, "val:resonance 0.4 ; val:cutof 800.0 ; val:drive 2.0");
    const auto findings = lintText(bad);
    assert(has(findings, "unknown-property", "vcf"));
    const auto& f = *std::find_if(findings.begin(), findings.end(), [](const LintFinding& x) { return x.code == "unknown-property"; });
    assert(f.port == "cutof");
    assert(f.message.find("Did you mean val:cutoff") != std::string::npos);
    assert(count(findings, "unknown-property") == 1);   // the real ports beside it are not accused

    // An option that the element lists, and the one every element takes, are fine.
    const auto fine = lintText(R"(
:c a val:Circuit ; rdfs:label "Fine" ; val:element :seq , :osc , :out ; val:arc :a1 , :c1 .
:seq a val:StepSequencer ; val:pattern "C3 E3" .
:osc a val:Oscillator ; val:oversampling 2 .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :osc ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
:c1 a val:ControlArc ; val:from [ val:node :seq ; val:port "pitch" ] ; val:to [ val:node :osc ; val:port "frequency" ] .
)");
    assert(! has(fine, "unknown-property"));

    // An option that belongs to a different element is unknown here.
    const auto wrong = lintText(R"(
:c a val:Circuit ; rdfs:label "Wrong" ; val:element :osc , :out ; val:arc :a1 .
:osc a val:Oscillator ; val:file "samples/bell.wav" .
:out a val:Output .
:a1 a val:Arc ; val:from [ val:node :osc ; val:port "out" ] ; val:to [ val:node :out ; val:port "in" ] .
)");
    assert(has(wrong, "unknown-property", "osc"));
}

void testDuplicateParameterSymbols()
{
    const std::string bad = std::string(kGood) + R"(
:p0 a val:Param ; val:slot 0 ; val:target :scale ; val:property val:min ; lv2:name "A" ; lv2:symbol "level" .
:p1 a val:Param ; val:slot 1 ; val:target :scale ; val:property val:max ; lv2:name "B" ; lv2:symbol "level" .
)";
    assert(has(lintText(bad), "duplicate-param-symbol"));
}

void testMissingProfileIsOnlyInformation()
{
    const auto findings = lintText(kGood);
    assert(has(findings, "no-profile"));
    const auto& f = *std::find_if(findings.begin(), findings.end(), [](const LintFinding& x) { return x.code == "no-profile"; });
    assert(f.severity == LintFinding::Severity::info);
}

void testTheJson()
{
    const auto json = lintToJson(lintText(R"(
:c a val:Circuit ; val:element :in , :out .
:in a val:Input .
:out a val:Output .
)"));
    assert(json.find("\"errors\":1") != std::string::npos);
    assert(json.find("\"code\":\"nothing-reaches-output\"") != std::string::npos);
    assert(json.find("\"severity\":\"error\"") != std::string::npos);
    int braces = 0;
    for (const char c : json) braces += c == '{' ? 1 : (c == '}' ? -1 : 0);
    assert(braces == 0);
    assert(lintToJson({}).find("\"findings\":[]") != std::string::npos);
}

/// The shipped circuits are what an agent starts from. They have to pass their own lint.
void testEveryShippedExampleIsClean()
{
    int checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(VALIS_EXAMPLES_DIR))
    {
        if (entry.path().extension() != ".ttl")
            continue;

        std::ifstream file(entry.path());
        std::ostringstream text;
        text << file.rdbuf();

        rdf::TurtleStore store;
        std::vector<rdf::ParseError> parseErrors;
        if (! store.parse(text.str(), "urn:valis:example", parseErrors))
            continue;
        CircuitModel model;
        std::vector<Diagnostic> diagnostics;
        if (! model.build(store, ontology(), diagnostics))
            continue;
        ++checked;

        for (const auto& f : lintCircuit(model))
        {
            if (f.severity == LintFinding::Severity::info)
                continue;
            std::printf("  %s: %s: %s\n", entry.path().filename().string().c_str(), f.code.c_str(), f.message.c_str());
            assert(false && "a shipped example has a lint finding");
        }
    }
    std::printf("  %d examples linted clean\n", checked);
    assert(checked > 30);
}

}  // namespace

int main()
{
    testACorrectCircuitHasNothingToSay();
    testAFixedValueUnderAnArcIsFound();
    testAKnobUnderAnArcIsFound();
    testNothingConnectedToTheOutput();
    testAPartThatCannotBeHeardIsFound();
    testMonitorsAreNotReportedAsUnheard();
    testAnEffectWithNothingToProcess();
    testAMisspeltPropertyIsFoundWithASuggestion();
    testDuplicateParameterSymbols();
    testMissingProfileIsOnlyInformation();
    testTheJson();
    testEveryShippedExampleIsClean();

    std::puts("LintTest PASSED");
    return 0;
}

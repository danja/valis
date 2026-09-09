// tests/console/ConsoleTest.cpp
//
// The console is a thin adapter over the ops, so it is exercised here with no
// editor constructed: local commands, Turtle extraction, the AI reply flow and
// the system prompt.

#include "valis/CircuitCompiler.h"
#include "valis/Console.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/Ops.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"
#include "valis/Vocabulary.h"

#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

using namespace valis;

namespace {

std::string readFile(const char* path)
{
    std::ifstream file(path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/// Stands in for the plugin, as in ops/OpDispatcherTest.cpp.
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
        const bool written = write(source, diagnostics);
        assert(written);
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

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

void testHelpAndUnknown()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    ConsoleSession console(host.ops());

    assert(contains(console.execute("help").text, "stats"));
    assert(contains(console.execute("help").text, "apply"));
    assert(contains(console.execute("HELP").text, "stats"));
    assert(contains(console.execute("nonsense").text, "unknown command"));
    assert(contains(console.execute("   ").text, ""));

    const auto cleared = console.execute("clear");
    assert(cleared.clear);
}

void testStatsParamsAndValidate()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    ConsoleSession console(host.ops());

    const auto stats = console.execute("stats").text;
    assert(contains(stats, "elements 4"));
    assert(contains(stats, "arcs 3"));

    assert(contains(console.execute("params").text, "Cutoff"));
    assert(console.execute("get 0").text == "800");
    assert(console.execute("set 0 900").text == "900");
    assert(console.execute("get 0").text == "900");
    assert(contains(console.execute("get 63").text, "no parameter"));
    assert(contains(console.execute("set").text, "usage"));
    assert(contains(console.execute("set x y").text, "usage"));

    assert(contains(console.execute("validate").text, "valid:"));
    assert(contains(console.execute("turtle").text, "val:Circuit"));

    assert(contains(console.execute("types ladder").text, "cutoff"));
    assert(contains(console.execute("types nosuchelement").text, "no element type"));
}

void testExtractTurtleBlocks()
{
    const auto fenced = ConsoleSession::extractTurtleBlocks(
        "Here is a filter:\n```turtle\n:x a val:Ladder .\n```\nEnjoy.");
    assert(fenced.size() == 1);
    assert(fenced[0] == ":x a val:Ladder .");

    const auto bare = ConsoleSession::extractTurtleBlocks(
        "```\n:x a val:Ladder .\n```");
    assert(bare.size() == 1);

    const auto json = ConsoleSession::extractTurtleBlocks(
        "```json\n{\"a\": 1}\n```");
    assert(json.empty());

    const auto two = ConsoleSession::extractTurtleBlocks(
        "first:\n```turtle\n:a a val:Gain .\n```\nsecond:\n```turtle\n:b a val:Gain .\n```");
    assert(two.size() == 2);

    const auto unclosed = ConsoleSession::extractTurtleBlocks(
        "truncated:\n```turtle\n:c a val:Gain .");
    assert(unclosed.size() == 1);

    assert(ConsoleSession::extractTurtleBlocks("just prose, no code").empty());
}

void testSystemPrompt()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    const auto prompt = ConsoleSession::buildSystemPrompt(host.ops().listElementTypes());

    assert(contains(prompt, "val:Ladder"));
    assert(contains(prompt, "cutoff"));
    assert(contains(prompt, "```turtle"));
    assert(contains(prompt, "val:UnitDelay"));
    assert(contains(prompt, "val:Output"));
    assert(contains(prompt, "REPLACES"));
}

void testUserPromptIncludesCircuit()
{
    Host host(readFile(VALIS_EXAMPLES_DIR "/basic.ttl"));
    const auto turtle = host.ops().getTurtle().value;

    const auto withCircuit = ConsoleSession::buildUserPrompt(turtle, "make it brighter");
    assert(contains(withCircuit, "make it brighter"));
    assert(contains(withCircuit, "```turtle"));
    assert(contains(withCircuit, "val:Circuit"));

    const auto withoutCircuit = ConsoleSession::buildUserPrompt("", "make it brighter");
    assert(withoutCircuit == "make it brighter");
}

void testAiReplyFlow()
{
    const std::string basic = readFile(VALIS_EXAMPLES_DIR "/basic.ttl");
    Host host(basic);
    ConsoleSession console(host.ops());

    assert(contains(console.execute("blocks").text, "no Turtle blocks"));
    assert(contains(console.execute("apply").text, "no Turtle blocks"));

    // A valid proposal installs and moves the bound knob.
    std::string hotter = basic;
    const auto at = hotter.find("800.0");
    assert(at != std::string::npos);
    hotter.replace(at, 5, "1234.0");

    const auto reply = console.noteAiResponse(
        "A brighter version:\n```turtle\n" + hotter + "\n```\nLet me know.");
    assert(contains(reply, "valid"));
    assert(contains(reply, "apply 0"));
    assert(console.pendingBlocks().size() == 1);
    assert(console.pendingBlocks()[0].valid);
    assert(contains(console.execute("blocks").text, "[0] valid"));

    const auto applied = console.applyBlock();
    assert(contains(applied.text, "installed block 0"));
    assert(console.execute("get 0").text == "1234");

    // An invalid proposal is reported and never installs.
    const auto bad = console.noteAiResponse(
        "Try this:\n```turtle\n:x a val:NoSuchElement .\n```");
    assert(contains(bad, "invalid"));
    assert(! console.pendingBlocks()[0].valid);
    const auto refused = console.execute("apply");
    assert(contains(refused.text, "no valid block"));

    // A reply with no code leaves nothing pending.
    const auto prose = console.noteAiResponse("I need more detail to help.");
    assert(contains(prose, "no Turtle blocks"));
    assert(console.pendingBlocks().empty());
}

int main()
{
    testHelpAndUnknown();
    testStatsParamsAndValidate();
    testExtractTurtleBlocks();
    testSystemPrompt();
    testUserPromptIncludesCircuit();
    testAiReplyFlow();
    return 0;
}

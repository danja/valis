// src/console/Console.cpp

#include "valis/Console.h"

#include "valis/Vocabulary.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>

namespace valis {

namespace {

std::string trim(const std::string& text)
{
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])))
        ++first;
    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])))
        --last;
    return text.substr(first, last - first);
}

std::string lower(std::string text)
{
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// Splits "command rest of line" into {"command", "rest"}.
std::pair<std::string, std::string> splitCommand(const std::string& line)
{
    const auto space = line.find_first_of(" \t");
    if (space == std::string::npos)
        return {lower(line), {}};
    return {lower(line.substr(0, space)), trim(line.substr(space + 1))};
}

/// Reads `"key":number-or-bool` out of the small fixed-shape JSON that
/// getDiagnostics emits. Returns fallback when the key is absent.
std::string jsonField(const std::string& json, const char* key, const std::string& fallback)
{
    const std::string needle = std::string("\"") + key + "\":";
    const auto at = json.find(needle);
    if (at == std::string::npos)
        return fallback;
    auto value = json.substr(at + needle.size());
    const auto end = value.find_first_of(",}");
    if (end != std::string::npos)
        value = value.substr(0, end);
    return trim(value);
}

std::string diagnosticsText(const std::vector<Diagnostic>& diagnostics)
{
    std::string out;
    for (const auto& d : diagnostics)
    {
        if (! out.empty())
            out += "\n";
        out += d.toString();
    }
    return out;
}

constexpr const char* kHelp =
    "commands:\n"
    "  help              this list\n"
    "  stats             elements, arcs, params and latency of the loaded circuit\n"
    "  turtle            the circuit's current Turtle source\n"
    "  types [filter]    element classes, with ports, ranges and units\n"
    "  params            parameter bindings with live values\n"
    "  get <slot>        one parameter value, in its own units\n"
    "  set <slot> <val>  one parameter value, in its own units\n"
    "  validate          check the current Turtle without reinstalling it\n"
    "  ai <prompt>       ask the AI to design a circuit (needs an API key)\n"
    "  blocks            Turtle blocks proposed by the AI, with validation\n"
    "  apply [n]         install block n (default: the most recent valid one)";

}  // namespace

// ---------------------------------------------------------------------------
// Local commands
// ---------------------------------------------------------------------------

ConsoleResult ConsoleSession::execute(const std::string& rawLine)
{
    const auto line = trim(rawLine);
    if (line.empty())
        return {""};

    const auto [command, args] = splitCommand(line);

    if (command == "help")
        return {kHelp};

    if (command == "clear")
        return {"", true};

    if (command == "stats")
    {
        const auto result = ops.getDiagnostics();
        if (! result.ok)
            return {"no circuit is loaded"};
        const auto& json = result.value;
        return {"elements " + jsonField(json, "elements", "?") +
                ", arcs " + jsonField(json, "arcs", "?") +
                ", params " + jsonField(json, "params", "?") +
                ", latency " + jsonField(json, "latency", "?") + " samples"};
    }

    if (command == "turtle")
    {
        const auto result = ops.getTurtle();
        if (! result.ok)
            return {"no circuit source is attached"};
        return {result.value};
    }

    if (command == "types")
    {
        std::string out;
        for (const auto& type : ops.listElementTypes())
        {
            const std::string name = "val:" + vocab::shortName(type.classIri);
            if (! args.empty() && lower(name).find(lower(args)) == std::string::npos)
                continue;
            if (! out.empty())
                out += "\n";
            out += name + (type.linear ? " (linear)" : " (nonlinear)");
            for (const auto& port : type.ports)
            {
                out += "\n  " + port.symbol + ": " + (port.control ? "control" : "audio") +
                       (port.input ? " in" : " out");
                if (port.control && port.input)
                {
                    std::ostringstream range;
                    range << " default=" << port.defaultValue
                          << " range=[" << port.minimum << "," << port.maximum << "]";
                    out += range.str();
                    if (! port.unit.empty())
                        out += " " + port.unit;
                }
            }
        }
        if (out.empty())
            return {"no element type matches '" + args + "'"};
        return {out};
    }

    if (command == "params")
    {
        const auto params = ops.listParams();
        if (params.empty())
            return {"no parameter bindings in this circuit"};
        std::string out;
        for (const auto& param : params)
        {
            if (! out.empty())
                out += "\n";
            std::ostringstream entry;
            entry << "[" << param.slot << "] " << param.name << " = " << param.value
                  << " [" << param.minimum << "," << param.maximum << "]";
            if (! param.unit.empty())
                entry << " " << param.unit;
            entry << " (" << param.targetNode << " " << param.property << ")";
            out += entry.str();
        }
        return {out};
    }

    if (command == "get")
    {
        int slot = -1;
        const auto parsed = std::from_chars(args.data(), args.data() + args.size(), slot);
        if (parsed.ec != std::errc{} || slot < 0)
            return {"usage: get <slot>"};
        const auto result = ops.getParam(slot);
        if (! result.ok)
            return {diagnosticsText(result.diagnostics)};
        return {result.value};
    }

    if (command == "set")
    {
        std::istringstream words(args);
        int slot = -1;
        double value = 0.0;
        if (! (words >> slot >> value))
            return {"usage: set <slot> <value>"};
        const auto result = ops.setParam(slot, value);
        if (! result.ok)
            return {diagnosticsText(result.diagnostics)};
        std::string out = result.value;
        if (! result.diagnostics.empty())
            out += "\n" + diagnosticsText(result.diagnostics);
        return {out};
    }

    if (command == "validate")
    {
        const auto turtle = ops.getTurtle();
        if (! turtle.ok)
            return {"no circuit source is attached"};
        const auto result = ops.validate(turtle.value);
        if (! result.ok)
            return {diagnosticsText(result.diagnostics)};
        return {"valid: " + result.value};
    }

    if (command == "blocks")
    {
        if (pending.empty())
            return {"no Turtle blocks proposed yet - ask with: ai <prompt>"};
        std::string out;
        for (std::size_t i = 0; i < pending.size(); ++i)
        {
            if (i > 0)
                out += "\n";
            out += "[" + std::to_string(i) + "] " +
                   (pending[i].valid ? "valid" : "invalid");
            if (! pending[i].valid)
                out += ": " + diagnosticsText(pending[i].diagnostics);
        }
        return {out};
    }

    if (command == "apply")
    {
        int index = -1;
        if (! args.empty())
        {
            const auto parsed = std::from_chars(args.data(), args.data() + args.size(), index);
            if (parsed.ec != std::errc{} || index < 0)
                return {"usage: apply [n]  (see: blocks)"};
        }
        return applyBlock(index);
    }

    if (command == "ai")
    {
        // The HTTP round-trip is asynchronous and lives in the UI layer, which
        // intercepts this command before it reaches here. Reaching here means
        // the UI did not handle it, so say what was expected.
        if (args.empty())
            return {"usage: ai <prompt>  (needs a Mistral API key - see Settings)"};
        return {"ai requests run from the Console tab"};
    }

    return {"unknown command '" + command + "' - try: help"};
}

ConsoleResult ConsoleSession::applyBlock(int index)
{
    if (pending.empty())
        return {"no Turtle blocks proposed yet - ask with: ai <prompt>"};

    const auto resolve = [&](int i) -> int
    {
        if (i >= 0)
            return i;
        // Default: the most recent valid block.
        for (int j = static_cast<int>(pending.size()) - 1; j >= 0; --j)
            if (pending[static_cast<std::size_t>(j)].valid)
                return j;
        return -1;
    };

    const int resolved = resolve(index);
    if (resolved < 0 || resolved >= static_cast<int>(pending.size()))
        return {"no valid block" + (index >= 0 ? " at index " + std::to_string(index) : "") +
                " - see: blocks"};

    const auto& block = pending[static_cast<std::size_t>(resolved)];
    if (! block.valid)
        return {"block " + std::to_string(resolved) + " is invalid:\n" +
                diagnosticsText(block.diagnostics)};

    // Revalidate on the way in: the ontology is unchanged, but the message is
    // cheap and a stale block must never half-install.
    const auto check = ops.validate(block.turtle);
    if (! check.ok)
        return {"block " + std::to_string(resolved) + " no longer validates:\n" +
                diagnosticsText(check.diagnostics)};

    const auto installed = ops.setTurtle(block.turtle);
    if (! installed.ok)
        return {"the circuit refused the block:\n" + diagnosticsText(installed.diagnostics)};

    return {"installed block " + std::to_string(resolved) + " (" + check.value + ")"};
}

// ---------------------------------------------------------------------------
// AI replies
// ---------------------------------------------------------------------------

std::vector<std::string> ConsoleSession::extractTurtleBlocks(const std::string& reply)
{
    std::vector<std::string> blocks;
    std::size_t pos = 0;

    while (true)
    {
        const auto open = reply.find("```", pos);
        if (open == std::string::npos)
            break;
        const auto tagEnd = reply.find('\n', open + 3);
        if (tagEnd == std::string::npos)
            break;
        const auto tag = trim(reply.substr(open + 3, tagEnd - open - 3));
        auto close = reply.find("```", tagEnd + 1);
        const auto body = trim(reply.substr(tagEnd + 1, close == std::string::npos
                                                             ? std::string::npos
                                                             : close - tagEnd - 1));
        if (lower(tag) == "turtle" || (tag.empty() && body.find("val:") != std::string::npos))
            if (! body.empty())
                blocks.push_back(body);
        if (close == std::string::npos)
            break;
        pos = close + 3;
    }

    return blocks;
}

std::string ConsoleSession::noteAiResponse(const std::string& reply)
{
    pending.clear();

    std::string out = trim(reply);
    if (out.empty())
        out = "(the model returned an empty reply)";

    const auto blocks = extractTurtleBlocks(reply);
    if (blocks.empty())
    {
        out += "\n\n[no Turtle blocks found - the reply held no ```turtle fence]";
        return out;
    }

    out += "\n";
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        const auto checked = ops.validate(blocks[i]);
        pending.push_back({blocks[i], checked.ok, checked.diagnostics});
        out += "\n[block " + std::to_string(i) + ": ";
        if (checked.ok)
            out += "valid (" + checked.value + ") - install with: apply " + std::to_string(i);
        else
            out += "invalid:\n" + diagnosticsText(checked.diagnostics);
        out += "]";
    }
    return out;
}

// ---------------------------------------------------------------------------
// System prompt
// ---------------------------------------------------------------------------

std::string ConsoleSession::buildSystemPrompt(const std::vector<ElementTypeInfo>& types)
{
    std::string prompt =
        "You design virtual-analog audio circuits for Valis, a DAW plugin whose "
        "circuits are Turtle RDF documents. Reply conversationally, and put each "
        "complete Turtle circuit in its own ```turtle fenced block. A circuit the "
        "user accepts is validated and installed as-is, so every block must be a "
        "complete document on its own, not a fragment.\n"
        "\n"
        "Document shape:\n"
        "- One `val:Circuit` naming its elements (`val:element`) and arcs (`val:arc`).\n"
        "- An element is `:id a val:Class ;` plus one `val:property value` per "
        "control port to set. A property matching a control port overrides its default.\n"
        "- An arc is `:a a val:Arc ; val:from [ val:node :src ; val:port \"out\" ] ; "
        "val:to [ val:node :dst ; val:port \"in\" ] .` From an output port to an "
        "input port, audio to audio, control to control.\n"
        "- A `val:Param` binds a host slot to an element property: `:p0 a val:Param ; "
        "val:slot 0 ; val:target :vcf ; val:property val:cutoff ;` plus lv2:name, "
        "lv2:symbol and units:unit. Expose the musically important controls.\n"
        "\n"
        "Rules the validator enforces - breaking one rejects the circuit:\n"
        "- Exactly one `val:Output`. Audio fan-in onto one input is an error unless "
        "the destination is a `val:Mixer`.\n"
        "- A feedback cycle must pass through a `val:UnitDelay`.\n"
        "- A control arc REPLACES the destination port's value each block: a fixed "
        "`val:cutoff` on an element is unreachable when a control arc also targets "
        "cutoff. Put the resting value in the control path (e.g. a Scale's `val:min`).\n"
        "- Control sources run before their destinations; an `val:Envelope` output "
        "is control-rate and drives control arcs, never audio inputs.\n"
        "- For drum voices with `val:TwinTBridge`, connect the amp envelope directly "
        "to the VCA cv and route `val:NoteGate` velocity to the TwinTBridge velocity "
        "port. Never route velocity through the VCA cv path: velocity is 0 on "
        "note-off and would close the VCA before the decay finishes.\n"
        "\n"
        "Always declare these prefixes:\n"
        "@prefix val: <http://purl.org/stuff/valis/> .\n"
        "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
        "@prefix units: <http://lv2plug.in/ns/extensions/units#> .\n"
        "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
        "@prefix : <urn:valis:circuit#> .\n"
        "\n"
        "Element catalogue (class, linearity, ports with defaults and ranges):\n";

    for (const auto& type : types)
    {
        prompt += "- val:" + vocab::shortName(type.classIri) +
                  (type.linear ? " (linear)" : " (nonlinear)") + ":";
        bool first = true;
        for (const auto& port : type.ports)
        {
            prompt += first ? " " : ", ";
            first = false;
            prompt += port.symbol + " " + (port.control ? "control" : "audio") +
                      (port.input ? "-in" : "-out");
            if (port.control && port.input)
            {
                std::ostringstream range;
                range << " [" << port.minimum << "," << port.maximum
                      << " default " << port.defaultValue << "]";
                prompt += range.str();
                if (! port.unit.empty())
                    prompt += " " + port.unit;
            }
        }
        prompt += "\n";
    }

    prompt +=
        "\n"
        "Minimal example (input, resonant filter, saturator, output):\n"
        "```turtle\n"
        "@prefix val: <http://purl.org/stuff/valis/> .\n"
        "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
        "@prefix units: <http://lv2plug.in/ns/extensions/units#> .\n"
        "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
        "@prefix : <urn:valis:circuit#> .\n"
        "\n"
        ":main a val:Circuit ;\n"
        "    val:element :in , :vcf , :drive , :out ;\n"
        "    val:arc :a1 , :a2 , :a3 .\n"
        "\n"
        ":in a val:Input .\n"
        ":vcf a val:Ladder ; val:cutoff 800.0 ; val:resonance 0.4 .\n"
        ":drive a val:Tanh ; val:gain 4.0 .\n"
        ":out a val:Output .\n"
        "\n"
        ":a1 a val:Arc ; val:from [ val:node :in ; val:port \"out\" ] ;\n"
        "    val:to [ val:node :vcf ; val:port \"in\" ] .\n"
        ":a2 a val:Arc ; val:from [ val:node :vcf ; val:port \"out\" ] ;\n"
        "    val:to [ val:node :drive ; val:port \"in\" ] .\n"
        ":a3 a val:Arc ; val:from [ val:node :drive ; val:port \"out\" ] ;\n"
        "    val:to [ val:node :out ; val:port \"in\" ] .\n"
        "\n"
        ":p0 a val:Param ; val:slot 0 ; val:target :vcf ; val:property val:cutoff ;\n"
        "    lv2:name \"Cutoff\" ; lv2:symbol \"cutoff\" ; units:unit units:hz .\n"
        "```\n"
        "Keep replies short. Explain the design in a sentence or two, then the "
        "circuit. Never invent element classes or ports outside the catalogue above.";

    return prompt;
}

}  // namespace valis

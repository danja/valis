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

std::string ConsoleSession::noteAiResponse(const std::string& reply, bool partialContext)
{
    pending.clear();

    std::string out = trim(reply);
    if (out.empty())
        out = "(the model returned an empty reply)";

    if (partialContext)
        out += "\n\n[the model was sent part of the circuit, not all of it: its "
               "Turtle is an edit to paste into the Code tab, not a complete "
               "circuit, so a block that fails validation here is expected]";

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

}  // namespace valis

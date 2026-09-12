// src/console/AiPrompt.cpp
//
// Everything that decides what is sent to a language model: the system prompt
// built from the ontology, the user message that carries the circuit, and the
// trimming that happens when the provider's per-minute token budget will not
// take the whole of it.
//
// It lives beside Console.cpp rather than inside it because the sizing rules
// are their own subject. Same class, same thread, no UI and no network.

#include "valis/Console.h"

#include "valis/RateLimits.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <cctype>
#include <map>
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

}  // namespace

// ---------------------------------------------------------------------------
// Sending part of a circuit
//
// A free tier's per-minute token budget caps one request, so a circuit larger
// than the budget can never be sent whole. Valis is better placed than most to
// send less: the document is structured, and a question about a filter does not
// need the drum voices. The cut is made on whole statements, so whatever is sent
// is still a parseable document.
// ---------------------------------------------------------------------------

namespace {

/// One Turtle statement with the comments and blank lines that precede it.
struct Chunk
{
    std::string text;
    std::string subject;    ///< local name without the leading colon
    std::string className;  ///< short class name after `a val:`
    std::vector<std::string> refs;  ///< local names this statement mentions
    bool directive = false; ///< @prefix / @base: always kept
};

bool isNameChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

/// Local names (`:kick`) mentioned in `text`, ignoring prefixed names such as
/// `val:node`, which have a prefix letter before the colon.
std::vector<std::string> localNames(const std::string& text)
{
    std::vector<std::string> names;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != ':')
            continue;
        if (i > 0 && (isNameChar(text[i - 1]) || text[i - 1] == '<'))
            continue;
        std::size_t end = i + 1;
        while (end < text.size() && isNameChar(text[end]))
            ++end;
        if (end > i + 1)
            names.push_back(text.substr(i + 1, end - i - 1));
        i = end - 1;
    }
    return names;
}

/// The class after `a val:` or `a :`, as a short name.
std::string classNameOf(const std::string& text)
{
    for (std::size_t i = 0; i + 2 < text.size(); ++i)
    {
        const bool boundary = (i == 0) || std::isspace(static_cast<unsigned char>(text[i - 1]));
        if (! boundary || text[i] != 'a' || ! std::isspace(static_cast<unsigned char>(text[i + 1])))
            continue;
        std::size_t start = i + 2;
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
            ++start;
        std::size_t end = start;
        while (end < text.size() && (isNameChar(text[end]) || text[end] == ':'))
            ++end;
        std::string term = text.substr(start, end - start);
        const auto colon = term.find(':');
        if (colon == std::string::npos)
            continue;
        return term.substr(colon + 1);
    }
    return {};
}

/// Splits a document into statements. A statement ends at a line whose last
/// non-space character is a full stop, which is how every circuit in examples/
/// and everything the compiler writes is laid out.
std::vector<Chunk> splitStatements(const std::string& turtle)
{
    std::vector<Chunk> chunks;
    std::istringstream lines(turtle);
    std::string line;
    std::string buffer;

    const auto flush = [&]()
    {
        if (buffer.empty())
            return;
        Chunk chunk;
        chunk.text = buffer;
        chunk.className = classNameOf(buffer);
        chunk.refs = localNames(buffer);

        // The subject and the @prefix test both read the first line that is
        // neither blank nor a comment: leading comments belong to the
        // statement they introduce.
        std::istringstream body(buffer);
        std::string bodyLine;
        while (std::getline(body, bodyLine))
        {
            const auto text = trim(bodyLine);
            if (text.empty() || text[0] == '#')
                continue;
            chunk.directive = text[0] == '@';
            if (const auto first = localNames(text); ! first.empty() && ! chunk.directive)
                chunk.subject = first.front();
            break;
        }

        chunks.push_back(std::move(chunk));
        buffer.clear();
    };

    while (std::getline(lines, line))
    {
        buffer += line;
        buffer += "\n";
        const auto text = trim(line);
        if (text.empty() || text[0] == '#')
            continue;
        if (text.back() == '.')
            flush();
    }
    flush();  // trailing comments, or a document that does not end in a stop
    return chunks;
}

/// Lowercases and reduces `text` to words: punctuation becomes a space and
/// hyphens close up, so a request for a "hi-hat" matches a comment that says
/// "Hi-Hats" and an id that says "hihat".
std::string wordsOf(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        if (c == '-' || c == '_')
            continue;
        out += std::isalnum(static_cast<unsigned char>(c)) != 0
            ? static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
            : ' ';
    }
    return out;
}

/// The words in a request worth searching a circuit for. Short words and the
/// vocabulary of every request ("make", "sound") match everything and so
/// select nothing.
std::vector<std::string> keywords(const std::string& request)
{
    static const std::vector<std::string> ignored = {
        "make", "more", "less", "with", "that", "this", "from", "into", "than",
        "then", "have", "want", "please", "sound", "sounds", "circuit", "change",
        "little", "much", "some", "just", "also", "does", "like", "give", "using",
        "should", "could", "would", "very", "bit"};

    std::vector<std::string> out;
    std::istringstream words(wordsOf(request));
    std::string word;
    while (words >> word)
    {
        if (word.size() < 4)
            continue;
        if (std::find(ignored.begin(), ignored.end(), word) != ignored.end())
            continue;
        if (std::find(out.begin(), out.end(), word) == out.end())
            out.push_back(word);
    }
    return out;
}

/// A statement without the comments and blank lines around it. Comments are
/// most of a hand-written circuit by volume, and under a budget that only a
/// fraction of the document fits into, the statements are worth more.
std::string statementOnly(const std::string& text)
{
    std::string out;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line))
    {
        const auto body = trim(line);
        if (body.empty() || body[0] == '#')
            continue;
        out += line;
        out += "\n";
    }
    return out;
}

/// Whether `word` appears in `text` as a word rather than inside a longer one.
bool mentionsWord(const std::string& text, const std::string& word)
{
    if (word.empty())
        return false;
    for (std::size_t at = text.find(word); at != std::string::npos;
         at = text.find(word, at + 1))
    {
        const bool leftOk = at == 0 || ! isNameChar(text[at - 1]);
        const std::size_t after = at + word.size();
        const bool rightOk = after >= text.size() || ! isNameChar(text[after]);
        if (leftOk && rightOk)
            return true;
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// System prompt
// ---------------------------------------------------------------------------

std::string ConsoleSession::buildUserPrompt(const std::string& currentTurtle,
                                            const std::string& request)
{
    const auto turtle = trim(currentTurtle);
    if (turtle.empty())
        return request;
    return "The circuit currently loaded is:\n"
           "```turtle\n" +
           turtle +
           "\n```\n"
           "Modify it as the request asks, and return the complete updated "
           "circuit in a ```turtle block.\n"
           "\n"
           "Request: " +
           request;
}

std::string ConsoleSession::excerptCircuit(const std::string& turtle,
                                           const std::string& request,
                                           std::size_t maxChars,
                                           std::string& summaryOut)
{
    summaryOut.clear();
    if (turtle.size() <= maxChars)
        return turtle;

    const auto chunks = splitStatements(turtle);

    const auto isElement = [](const Chunk& chunk)
    {
        return ! chunk.subject.empty() && chunk.className != "Arc" &&
               chunk.className != "Param" && chunk.className != "Circuit" &&
               ! chunk.directive;
    };

    // The frame is the prefixes and a declaration rebuilt from what survives
    // the cut. Reusing the original declaration is not an option: in a drum
    // machine it names every element and arc and is itself larger than the
    // budget being cut to.
    std::string prefixes;
    std::string circuitSubject = "circuit";
    for (const auto& chunk : chunks)
    {
        if (! chunk.directive)
        {
            if (chunk.className == "Circuit" && ! chunk.subject.empty())
                circuitSubject = chunk.subject;
            continue;
        }
        std::istringstream lines(chunk.text);
        std::string line;
        while (std::getline(lines, line))
            if (const auto text = trim(line); ! text.empty() && text[0] == '@')
                prefixes += text + "\n";
    }

    std::size_t size = prefixes.size() + circuitSubject.size() + 40;
    if (size >= maxChars)
        return turtle;  // nothing useful can be said in this little room

    // Seed with whatever the request names: an element id, a class, or the
    // comment above a voice ("# Closed Hi-Hat"). The output and the input go in
    // regardless, since they are what "make it brighter" is implicitly about.
    const auto wanted = keywords(request);
    std::vector<std::string> frontier, endpoints;
    for (const auto& chunk : chunks)
    {
        if (! isElement(chunk))
            continue;
        if (chunk.className == "Output" || chunk.className == "Input")
        {
            endpoints.push_back(chunk.subject);
            continue;
        }
        const auto haystack = wordsOf(chunk.text);
        for (const auto& word : wanted)
            if (mentionsWord(haystack, word))
            {
                frontier.push_back(chunk.subject);
                break;
            }
    }
    // What the request names is spent on first; the endpoints of the signal
    // path come after, with whatever room is left.
    frontier.insert(frontier.end(), endpoints.begin(), endpoints.end());

    std::vector<bool> keep(chunks.size(), false);
    std::vector<std::string> keptElements, keptArcs, selected;
    const auto isSelected = [&selected](const std::string& name)
    {
        return std::find(selected.begin(), selected.end(), name) != selected.end();
    };

    // Grow outwards one hop at a time: the named elements, then the arcs that
    // touch them, then what those arcs reach. Every addition is a whole
    // statement plus its entry in the rebuilt declaration, and growth stops at
    // the first one that will not fit.
    // A mixer is connected to every voice in the machine. Following all of its
    // arcs would spend the whole budget on the fan-in of one hub and leave
    // nothing for what the request was about, so each element contributes only
    // its first few arcs.
    constexpr std::size_t kMaxArcsPerElement = 6;

    while (! frontier.empty() && size + 200 < maxChars)
    {
        std::vector<std::string> next;
        for (const auto& name : frontier)
        {
            if (isSelected(name))
                continue;
            selected.push_back(name);

            std::size_t arcsHere = 0;
            for (std::size_t i = 0; i < chunks.size(); ++i)
            {
                const auto& chunk = chunks[i];
                if (keep[i] || chunk.directive || chunk.className == "Circuit")
                    continue;
                const bool touches = chunk.subject == name ||
                    std::find(chunk.refs.begin(), chunk.refs.end(), name) != chunk.refs.end();
                if (! touches)
                    continue;
                if (chunk.className == "Arc" && arcsHere >= kMaxArcsPerElement)
                    continue;

                const std::size_t cost = statementOnly(chunk.text).size() +
                                         chunk.subject.size() + 4;
                if (size + cost > maxChars)
                    continue;  // too big for what is left: a later one may fit
                keep[i] = true;
                size += cost;

                if (chunk.className == "Arc")
                {
                    ++arcsHere;
                    keptArcs.push_back(chunk.subject);
                    for (const auto& ref : chunk.refs)
                        if (! isSelected(ref))
                            next.push_back(ref);
                }
                else if (isElement(chunk))
                    keptElements.push_back(chunk.subject);
            }
        }
        frontier = std::move(next);
    }

    std::size_t omitted = 0;
    std::map<std::string, int> omittedClasses;
    for (std::size_t i = 0; i < chunks.size(); ++i)
    {
        if (keep[i] || ! isElement(chunks[i]))
            continue;
        ++omitted;
        ++omittedClasses[chunks[i].className];
    }

    if (omitted == 0)
        return turtle;

    const auto list = [](const std::vector<std::string>& names)
    {
        std::string out;
        for (std::size_t i = 0; i < names.size(); ++i)
            out += (i == 0 ? " :" : " , :") + names[i];
        return out;
    };

    std::string out = prefixes + "\n:" + circuitSubject + " a val:Circuit ;\n";
    if (! keptElements.empty())
        out += "    val:element" + list(keptElements) + (keptArcs.empty() ? " .\n" : " ;\n");
    if (! keptArcs.empty())
        out += "    val:arc" + list(keptArcs) + " .\n";
    if (keptElements.empty() && keptArcs.empty())
        out += "    .\n";
    out += "\n";
    for (std::size_t i = 0; i < chunks.size(); ++i)
        if (keep[i])
            out += statementOnly(chunks[i].text) + "\n";

    // Naming what is missing costs a dozen tokens and stops the model from
    // assuming the circuit is what it can see.
    std::vector<std::pair<int, std::string>> ranked;
    ranked.reserve(omittedClasses.size());
    for (const auto& entry : omittedClasses)
        ranked.push_back({entry.second, entry.first});
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b)
    {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });

    summaryOut = std::to_string(keptElements.size()) + " of " +
                 std::to_string(keptElements.size() + omitted) +
                 " elements are shown in full, with the arcs between them. The other " +
                 std::to_string(omitted) + " elements and their arcs are still in the "
                 "circuit but are not shown here: ";
    for (std::size_t i = 0; i < ranked.size() && i < 4; ++i)
    {
        if (i > 0)
            summaryOut += ", ";
        summaryOut += std::to_string(ranked[i].first) + " val:" + ranked[i].second;
    }
    if (ranked.size() > 4)
        summaryOut += ", and others";
    summaryOut += ".";
    return out;
}

AiPrompt ConsoleSession::buildPrompt(const std::vector<ElementTypeInfo>& types,
                                     const std::string& currentTurtle,
                                     const std::string& request,
                                     long long budgetTokens)
{
    AiPrompt out;
    out.system = buildSystemPrompt(types);
    out.user = buildUserPrompt(currentTurtle, request);

    const auto estimate = [&]
    {
        out.estimatedTokens = ai::estimateTokens(out.system + out.user);
        return out.estimatedTokens;
    };

    if (estimate() <= budgetTokens || budgetTokens <= 0)
        return out;

    // The catalogue is the largest fixed cost and the easiest to cut: the
    // classes the circuit already uses, and any the request names, keep their
    // ports; the rest keep only their names, so nothing is invented that the
    // compiler would reject.
    std::vector<std::string> detailed;
    const std::string haystack = lower(currentTurtle) + "\n" + lower(request);
    for (const auto& type : types)
    {
        const auto name = vocab::shortName(type.classIri);
        if (mentionsWord(haystack, lower(name)))
            detailed.push_back(name);
    }
    out.system = buildSystemPrompt(types, detailed);
    out.catalogueCondensed = true;
    if (estimate() <= budgetTokens)
    {
        out.note = "the element catalogue was sent in short form to fit the "
                   "provider's token budget";
        return out;
    }

    // Then the circuit itself. What is left of the budget, in characters, at
    // the code rate: an estimate that reads as a floor, so the cut is made a
    // little deeper than strictly needed rather than a little shallower.
    const long long fixed = ai::estimateTokens(out.system) +
                            ai::estimateTokens(request, ai::TextKind::prose) + 200;
    const long long spare = budgetTokens - fixed;
    if (spare <= 0)
    {
        out.note = "even without the circuit this prompt exceeds the provider's "
                   "token budget: pick a provider with a larger budget under "
                   "Settings > AI Provider";
        return out;
    }

    const auto maxChars = static_cast<std::size_t>(
        static_cast<double>(spare) / ai::kTokensPerCharCode);
    std::string summary;
    const auto excerpt = excerptCircuit(currentTurtle, request, maxChars, summary);
    if (summary.empty())
        return out;  // nothing could be cut: let the provider have the last word

    out.circuitExcerpted = true;
    out.note = summary;
    out.user = "The circuit currently loaded is too large to send in full. This "
               "is the part of it that relates to the request:\n"
               "```turtle\n" + trim(excerpt) + "\n```\n" +
               summary + "\n"
               "Reply with the elements and arcs you would change or add, in a "
               "```turtle block, and say in words what to change. Do not "
               "reproduce the parts you were not shown, and do not return a "
               "complete circuit: what you cannot see must stay as it is.\n"
               "\n"
               "Request: " + request;
    estimate();
    return out;
}

std::string ConsoleSession::buildSystemPrompt(const std::vector<ElementTypeInfo>& types)
{
    // Every class in full: no budget pressure, nothing to leave out.
    std::vector<std::string> all;
    all.reserve(types.size());
    for (const auto& type : types)
        all.push_back(vocab::shortName(type.classIri));
    return buildSystemPrompt(types, all);
}

std::string ConsoleSession::buildSystemPrompt(const std::vector<ElementTypeInfo>& types,
                                              const std::vector<std::string>& detailedClasses)
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

    std::string brief;
    for (const auto& type : types)
    {
        const auto name = vocab::shortName(type.classIri);
        if (std::find(detailedClasses.begin(), detailedClasses.end(), name) ==
            detailedClasses.end())
        {
            // Named but not described. The model still knows the class exists
            // and can ask for it; it just cannot see the ports yet.
            brief += (brief.empty() ? "" : ", ") + name;
            continue;
        }

        prompt += "- val:" + name +
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

    if (! brief.empty())
        prompt += "\nOther classes exist but their ports are not listed here. Ask "
                  "before using one: " + brief + "\n";

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

// tests/docs/GuideTest.cpp
//
// docs/guide-for-agents.md is read by a program that does exactly what it says, so a
// stale line in it costs somebody an afternoon. This holds it to what the program does:
// every element class and port it names exists, every MCP tool is described and every
// tool it describes exists, and every file it points at is there.
//
// The tool names are read from src/mcp/McpServer.cpp as text, because the server is part
// of the plugin and not of the library the tests link.

#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace valis;

namespace {

std::string slurp(const std::string& path)
{
    std::ifstream file(path);
    assert(file.good() && "cannot open a file the guide test needs");
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/// The part of the guide under a heading, up to the next heading of the same level or above.
std::string section(const std::string& guide, const std::string& heading)
{
    const auto start = guide.find(heading);
    assert(start != std::string::npos && "a heading this test reads has gone from the guide");
    const auto level = heading.find(' ');
    const std::string next = "\n" + heading.substr(0, level + 1);
    const auto end = guide.find(next, start + heading.size());
    return guide.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::vector<std::string> backticked(const std::string& text)
{
    std::vector<std::string> found;
    static const std::regex pattern("`([^`]+)`");
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it)
        found.push_back((*it)[1]);
    return found;
}

void testElementTableMatchesTheOntology(const Ontology& ontology, const std::string& guide)
{
    const auto table = section(guide, "## Choosing an element");

    int rows = 0;
    std::istringstream lines(table);
    for (std::string line; std::getline(lines, line);)
    {
        static const std::regex row("^\\| `val:(\\w+)` \\|[^|]*\\| (.*) \\|$");
        std::smatch match;
        if (! std::regex_match(line, match, row))
            continue;
        ++rows;

        const std::string name = match[1];
        const auto* type = ontology.find(vocab::valTerm(name));
        if (type == nullptr)
            std::printf("  the guide names val:%s, which the ontology does not declare\n", name.c_str());
        assert(type != nullptr);

        for (const auto& port : backticked(match[2]))
        {
            if (type->findPort(port) == nullptr)
                std::printf("  the guide gives val:%s a port '%s' that it does not have\n", name.c_str(), port.c_str());
            assert(type->findPort(port) != nullptr);
        }
    }
    std::printf("  %d element rows checked\n", rows);
    assert(rows > 20);
}

void testEveryClassMentionedExists(const Ontology& ontology, const std::string& guide)
{
    // Classes that are not elements but are real.
    const std::set<std::string> others{"Circuit", "Arc", "ControlArc", "Param", "Subcircuit",
                                       "BufferPort", "NoteGate"};

    static const std::regex mention("val:([A-Z]\\w+)");
    for (std::sregex_iterator it(guide.begin(), guide.end(), mention), end; it != end; ++it)
    {
        const std::string name = (*it)[1];
        const bool known = ontology.find(vocab::valTerm(name)) != nullptr || others.count(name) != 0;
        if (! known)
            std::printf("  the guide mentions val:%s, which is not a class\n", name.c_str());
        assert(known);
    }
}

void testEveryToolIsDescribedAndEveryDescribedToolExists(const std::string& guide)
{
    const auto source = slurp(VALIS_ROOT_DIR "/src/mcp/McpServer.cpp");
    std::set<std::string> real;
    static const std::regex tool("\n    \\{\"([a-z_]+)\", \"");
    for (std::sregex_iterator it(source.begin(), source.end(), tool), end; it != end; ++it)
        real.insert((*it)[1]);
    assert(real.size() > 15);

    std::set<std::string> described;
    const auto table = section(guide, "## The tools");
    std::istringstream lines(table);
    for (std::string line; std::getline(lines, line);)
    {
        static const std::regex row("^\\| ((?:`[a-z_]+`,? ?)+) \\|");
        std::smatch match;
        if (std::regex_search(line, match, row))
            for (const auto& name : backticked(match[1]))
                described.insert(name);
    }

    for (const auto& name : real)
        if (described.count(name) == 0)
            std::printf("  the MCP tool %s is not in the guide\n", name.c_str());
    for (const auto& name : described)
        if (real.count(name) == 0)
            std::printf("  the guide describes a tool %s that the server does not have\n", name.c_str());

    assert(real == described);
    std::printf("  %zu tools, all described\n", real.size());
}

void testEveryFileItPointsAtExists(const std::string& guide)
{
    const std::filesystem::path root(VALIS_ROOT_DIR);

    static const std::regex example("examples/([A-Za-z0-9_.-]+\\.ttl)");
    for (std::sregex_iterator it(guide.begin(), guide.end(), example), end; it != end; ++it)
    {
        const auto path = root / "examples" / std::string((*it)[1]);
        if (! std::filesystem::exists(path))
            std::printf("  the guide points at %s, which is not there\n", path.string().c_str());
        assert(std::filesystem::exists(path));
    }

    static const std::regex link("\\]\\(([A-Za-z0-9_./-]+\\.md)\\)");
    for (std::sregex_iterator it(guide.begin(), guide.end(), link), end; it != end; ++it)
    {
        const auto path = root / "docs" / std::string((*it)[1]);
        if (! std::filesystem::exists(path))
            std::printf("  the guide links to %s, which is not there\n", path.string().c_str());
        assert(std::filesystem::exists(path));
    }
}

}  // namespace

int main()
{
    Ontology ontology;
    std::vector<std::string> errors;
    ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
    const bool loaded = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
    assert(loaded);

    const auto guide = slurp(VALIS_ROOT_DIR "/docs/guide-for-agents.md");

    testElementTableMatchesTheOntology(ontology, guide);
    testEveryClassMentionedExists(ontology, guide);
    testEveryToolIsDescribedAndEveryDescribedToolExists(guide);
    testEveryFileItPointsAtExists(guide);

    std::puts("GuideTest PASSED");
    return 0;
}

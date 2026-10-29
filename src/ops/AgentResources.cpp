// src/ops/AgentResources.cpp

#include "valis/AgentResources.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace valis {

namespace {

namespace fs = std::filesystem;

constexpr const char* kGuideUri = "valis://guide";
constexpr const char* kIndexUri = "valis://examples";
constexpr const char* kExamplePrefix = "valis://examples/";

std::optional<std::string> slurp(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (! file.good())
        return std::nullopt;
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/// What an example says about itself: the comment block at the top of the file, after the line
/// that names the file. A sentence or two, enough to choose by.
std::string summaryOf(const std::string& turtle)
{
    std::istringstream lines(turtle);
    std::string line, summary;
    bool started = false;

    while (std::getline(lines, line))
    {
        if (line.rfind('#', 0) != 0)
        {
            // Before the description starts, blank lines and the prefix declarations that some
            // files put first are skipped over.
            if (! started && (line.empty() || line.rfind("@prefix", 0) == 0))
                continue;
            break;
        }

        auto text = line.substr(1);
        if (! text.empty() && text.front() == ' ')
            text.erase(0, 1);

        // The first line often names the file; an empty one ends a paragraph.
        if (! started)
        {
            if (text.empty() || text.rfind("examples/", 0) == 0)
                continue;
            started = true;
        }
        else if (text.empty())
            break;

        summary += (summary.empty() ? "" : " ") + text;
        if (summary.size() > 320)
            break;
    }

    if (summary.size() > 320)
        summary = summary.substr(0, summary.rfind(' ', 320)) + " ...";
    return summary;
}

std::vector<std::string> exampleNames(const std::string& examplesDirectory)
{
    std::vector<std::string> names;
    std::error_code ignored;
    for (const auto& entry : fs::directory_iterator(examplesDirectory, ignored))
        if (entry.is_regular_file() && entry.path().extension() == ".ttl" && isExampleName(entry.path().stem().string()))
            names.push_back(entry.path().stem().string());

    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace

bool isExampleName(const std::string& name)
{
    if (name.empty() || name.size() > 80 || name.front() == '.')
        return false;

    return std::all_of(name.begin(), name.end(), [](unsigned char c)
                       { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; })
        && name.find("..") == std::string::npos;
}

std::vector<AgentResource> listAgentResources(const std::string& rootDirectory, const std::string& examplesDirectory)
{
    std::vector<AgentResource> list;

    if (fs::exists(fs::path(rootDirectory) / "docs" / "guide-for-agents.md"))
        list.push_back({kGuideUri, "Guide for agents",
                        "How to build and modify circuits in Valis through these tools: concepts, workflow, patterns and "
                        "common mistakes. Read it first.",
                        "text/markdown"});

    const auto names = exampleNames(examplesDirectory);
    if (names.empty())
        return list;

    list.push_back({kIndexUri, "Examples",
                    "Every circuit in examples/ with what it is, to choose a starting point from. Read one with "
                    "valis://examples/<name>.",
                    "text/plain"});

    for (const auto& name : names)
    {
        const auto text = slurp(fs::path(examplesDirectory) / (name + ".ttl"));
        list.push_back({std::string(kExamplePrefix) + name, "Example: " + name,
                        text ? summaryOf(*text) : std::string(), "text/turtle"});
    }
    return list;
}

std::optional<std::string> readAgentResource(const std::string& rootDirectory, const std::string& examplesDirectory,
                                             const std::string& uri)
{
    if (uri == kGuideUri)
        return slurp(fs::path(rootDirectory) / "docs" / "guide-for-agents.md");

    if (uri == kIndexUri)
    {
        std::string index = "Circuits in examples/. Read one with valis://examples/<name>, or load it with load_file.\n";
        for (const auto& name : exampleNames(examplesDirectory))
        {
            const auto text = slurp(fs::path(examplesDirectory) / (name + ".ttl"));
            index += "\n" + name + "\n  " + (text ? summaryOf(*text) : std::string()) + "\n";
        }
        return index;
    }

    if (uri.rfind(kExamplePrefix, 0) == 0)
    {
        const auto name = uri.substr(std::char_traits<char>::length(kExamplePrefix));
        if (! isExampleName(name))
            return std::nullopt;
        return slurp(fs::path(examplesDirectory) / (name + ".ttl"));
    }

    return std::nullopt;
}

}  // namespace valis

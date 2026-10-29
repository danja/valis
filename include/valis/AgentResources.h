// include/valis/AgentResources.h
//
// The files an agent reads to learn Valis, offered as MCP resources so it does not need
// access to the repository: the guide, and every circuit in examples/ to start from.
//
//   valis://guide                  docs/guide-for-agents.md
//   valis://examples               an index: each example's name and what it is
//   valis://examples/<name>        one example's Turtle, such as valis://examples/basic
//
// Plain files, read on request. The name in a URI is checked before it touches the file
// system, so a URI cannot name a path outside examples/.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace valis {

struct AgentResource
{
    std::string uri, name, description, mimeType;
};

/// The guide, the index, and one entry per example, in name order. Empty entries for a
/// folder that is not there.
std::vector<AgentResource> listAgentResources(const std::string& rootDirectory,
                                              const std::string& examplesDirectory);

/// The text behind a URI from listAgentResources, or nothing if it is not one of them (or the
/// file is gone).
std::optional<std::string> readAgentResource(const std::string& rootDirectory,
                                             const std::string& examplesDirectory,
                                             const std::string& uri);

/// Whether `name` could be an example's name: letters, digits, dot, dash and underscore, and
/// not a path. Exposed so the check can be tested on its own.
bool isExampleName(const std::string& name);

}  // namespace valis

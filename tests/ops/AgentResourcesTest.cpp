// tests/ops/AgentResourcesTest.cpp
//
// The guide and the examples as MCP resources. They are served from files named by a URI an
// agent chose, so the name is checked before it reaches the file system.

#include "valis/AgentResources.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>

using namespace valis;

namespace {

const std::string kRoot = VALIS_ROOT_DIR;
const std::string kExamples = VALIS_EXAMPLES_DIR;

const AgentResource* find(const std::vector<AgentResource>& list, const std::string& uri)
{
    const auto it = std::find_if(list.begin(), list.end(), [&](const AgentResource& r) { return r.uri == uri; });
    return it != list.end() ? &*it : nullptr;
}

void testTheListNamesTheGuideTheIndexAndEveryExample()
{
    const auto list = listAgentResources(kRoot, kExamples);

    const auto* guide = find(list, "valis://guide");
    assert(guide != nullptr && guide->mimeType == "text/markdown");
    assert(find(list, "valis://examples") != nullptr);

    const auto* basic = find(list, "valis://examples/basic");
    assert(basic != nullptr && basic->mimeType == "text/turtle");
    assert(basic->description.find("smallest circuit") != std::string::npos);   // from its header comment
    assert(basic->description.find("examples/basic.ttl") == std::string::npos); // not the line that names the file

    // One for each .ttl file, and the descriptions are short enough to scan.
    assert(list.size() > 40);
    for (const auto& item : list)
    {
        if (item.description.empty()) std::printf("  no description: %s\n", item.uri.c_str());
        assert(! item.description.empty());
        assert(item.description.size() < 400);
    }
    std::printf("  %zu resources\n", list.size());
}

void testReadingThem()
{
    const auto guide = readAgentResource(kRoot, kExamples, "valis://guide");
    assert(guide && guide->find("Guide for Agents") != std::string::npos);

    const auto basic = readAgentResource(kRoot, kExamples, "valis://examples/basic");
    assert(basic && basic->find("val:Circuit") != std::string::npos);

    const auto index = readAgentResource(kRoot, kExamples, "valis://examples");
    assert(index && index->find("dd3") != std::string::npos && index->find("grain-clouds") != std::string::npos);
    assert(index->find("bucket-brigade") != std::string::npos || index->find("BBD") != std::string::npos || index->find("delay") != std::string::npos);
}

void testNamesThatReachOutsideAreRefused()
{
    for (const char* name : {"..", "../README", "../../etc/passwd", "a/b", "a\\b", ".hidden", "x..y", "", "name with space",
                             "/etc/passwd", "basic.ttl/../../CLAUDE"})
        assert(! isExampleName(name));
    for (const char* name : {"basic", "dd3", "grain-clouds", "rings_modal", "v1.2"})
        assert(isExampleName(name));

    for (const char* uri : {"valis://examples/../CLAUDE", "valis://examples/../../etc/passwd", "valis://examples//etc/passwd",
                            "valis://examples/%2e%2e/CLAUDE", "valis://examples/nonexistent", "valis://guide/extra", "valis://other"})
        assert(! readAgentResource(kRoot, kExamples, uri));

    // A folder that is not there lists nothing rather than failing.
    assert(listAgentResources("/no/such/root", "/no/such/examples").empty());
    assert(! readAgentResource("/no/such/root", "/no/such/examples", "valis://guide"));
}

}  // namespace

int main()
{
    testTheListNamesTheGuideTheIndexAndEveryExample();
    testReadingThem();
    testNamesThatReachOutsideAreRefused();

    std::puts("AgentResourcesTest PASSED");
    return 0;
}

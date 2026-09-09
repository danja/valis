// tests/ai/AiProvidersTest.cpp
//
// The provider presets are pure data driving the Settings menu: names unique,
// endpoints well-formed, every entry resolvable by lookup.

#include "valis/AiProviders.h"

#include <cassert>
#include <string>

using namespace valis::ai;

namespace {

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

void testProviderTable()
{
    const auto& providers = aiProviders();
    assert(! providers.empty());

    // Mistral stays first: it is the default endpoint.
    assert(providers.front().name == "Mistral");

    for (std::size_t i = 0; i < providers.size(); ++i)
    {
        const auto& p = providers[i];
        assert(! p.name.empty());
        assert(! p.endpoint.empty());
        assert(! p.model.empty());
        assert(startsWith(p.endpoint, "https://") || startsWith(p.endpoint, "http://localhost"));
        if (p.needsKey)
            assert(! p.keyHint.empty());

        // Names unique; every endpoint resolves back to its entry.
        for (std::size_t j = i + 1; j < providers.size(); ++j)
            assert(providers[j].name != p.name);
        const auto* found = findAiProvider(p.endpoint);
        assert(found != nullptr);
        assert(found->name == p.name);
    }

    assert(findAiProvider("https://example.com/v1/chat/completions") == nullptr);
}

int main()
{
    testProviderTable();
    return 0;
}

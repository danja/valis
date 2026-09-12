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

    // Mistral stays first: it is the default endpoint, and the list is the
    // order the router falls through, so the provider with the largest
    // measured token budget leads.
    assert(providers.front().name == "Mistral");

    for (std::size_t i = 0; i < providers.size(); ++i)
    {
        const auto& p = providers[i];
        assert(! p.name.empty());
        assert(! p.endpoint.empty());
        assert(! p.model.empty());
        assert(startsWith(p.endpoint, "https://") || startsWith(p.endpoint, "http://localhost"));
        if (p.needsKey)
        {
            assert(! p.keyHint.empty());
            // A keyed provider is only a usable fallback if a key can be found
            // without the user selecting it first.
            assert(! p.keyEnvVar.empty());
        }
        else
            assert(p.keyEnvVar.empty());

        // A provider either names all the headers it reports or names none:
        // half a map would read a limit with no remaining count, which looks
        // like an exhausted budget rather than an unmeasured one.
        if (p.headers.reportsAnything())
        {
            assert(! p.headers.limitTokens.empty());
            assert(! p.headers.remainingTokens.empty());
        }

        // Names unique; every endpoint resolves back to its entry.
        for (std::size_t j = i + 1; j < providers.size(); ++j)
            assert(providers[j].name != p.name);
        const auto* found = findAiProvider(p.endpoint);
        assert(found != nullptr);
        assert(found->name == p.name);
    }

    assert(findAiProvider("https://example.com/v1/chat/completions") == nullptr);

    // Groq and Mistral report their budgets under different names. Reading one
    // provider's headers with another's names measures nothing at all.
    const auto* groq = findAiProvider("https://api.groq.com/openai/v1/chat/completions");
    const auto* mistral = findAiProvider("https://api.mistral.ai/v1/chat/completions");
    assert(groq != nullptr && mistral != nullptr);
    assert(groq->headers.limitTokens != mistral->headers.limitTokens);
    assert(! groq->headers.resetTokens.empty());
    assert(mistral->headers.resetTokens.empty());  // Mistral sends no reset

    const std::string groqHeaders = "x-ratelimit-limit-tokens: 8000\r\n";
    assert(parseRateLimitHeaders(groqHeaders, groq->headers).limitTokens == 8000);
    assert(! parseRateLimitHeaders(groqHeaders, mistral->headers).limitKnown());
}

int main()
{
    testProviderTable();
    return 0;
}

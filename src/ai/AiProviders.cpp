// src/ai/AiProviders.cpp

#include "valis/AiProviders.h"

namespace valis {
namespace ai {

const std::vector<AiProvider>& aiProviders()
{
    // Endpoints and free tiers verified September 2026. Model ids rot faster
    // than endpoints: each entry names a long-lived default, and Settings >
    // Set AI Model overrides it without touching anything else.
    static const std::vector<AiProvider> providers = {
        {"Mistral",
         "https://api.mistral.ai/v1/chat/completions",
         "mistral-small-latest",
         true,
         "console.mistral.ai"},
        {"Groq",
         "https://api.groq.com/openai/v1/chat/completions",
         "openai/gpt-oss-20b",
         true,
         "console.groq.com/keys (free, no card)"},
        {"Gemini",
         "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions",
         "gemini-3.5-flash",
         true,
         "Google AI Studio (free tier, no card)"},
        {"OpenRouter",
         "https://openrouter.ai/api/v1/chat/completions",
         "openrouter/free",
         true,
         "openrouter.ai/keys (free, no card; openrouter/free auto-picks a free model)"},
        {"Ollama (local)",
         "http://localhost:11434/v1/chat/completions",
         "llama3.1",
         false,
         "no key: install ollama and run: ollama pull llama3.1"},
    };
    return providers;
}

const AiProvider* findAiProvider(const std::string& endpoint)
{
    for (const auto& provider : aiProviders())
        if (provider.endpoint == endpoint)
            return &provider;
    return nullptr;
}

}  // namespace ai
}  // namespace valis

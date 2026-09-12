// src/ai/AiProviders.cpp

#include "valis/AiProviders.h"

namespace valis {
namespace ai {

namespace {

/// Groq and other OpenAI-compatible tiers use the unsuffixed names and send a
/// reset duration with every response.
RateLimitHeaderNames openAiStyleHeaders()
{
    return {"x-ratelimit-limit-tokens",   "x-ratelimit-remaining-tokens",
            "x-ratelimit-reset-tokens",   "x-ratelimit-limit-requests",
            "x-ratelimit-remaining-requests", "x-ratelimit-reset-requests"};
}

/// Mistral states the window in the header name and sends no reset at all, so
/// its remaining count has to be held more conservatively than Groq's despite
/// being far larger.
RateLimitHeaderNames mistralHeaders()
{
    return {"x-ratelimit-limit-tokens-minute", "x-ratelimit-remaining-tokens-minute",
            {},                                "x-ratelimit-limit-req-minute",
            "x-ratelimit-remaining-req-minute", {}};
}

}  // namespace

const std::vector<AiProvider>& aiProviders()
{
    // Endpoints and free tiers verified September 2026. Model ids rot faster
    // than endpoints: each entry names a long-lived default, and Settings >
    // Set AI Model overrides it without touching anything else.
    //
    // Order is rotation order. Mistral leads because its measured budget is
    // 625,000 tokens per minute against Groq's 8,000, and a Valis prompt
    // carries a whole circuit: a document Groq can never accept is
    // unremarkable to Mistral.
    static const std::vector<AiProvider> providers = {
        {"Mistral",
         "https://api.mistral.ai/v1/chat/completions",
         "mistral-small-latest",
         true,
         "console.mistral.ai",
         "MISTRAL_API_KEY",
         mistralHeaders()},
        {"Groq",
         "https://api.groq.com/openai/v1/chat/completions",
         "openai/gpt-oss-20b",
         true,
         "console.groq.com/keys (free, no card)",
         "GROQ_API_KEY",
         openAiStyleHeaders()},
        {"Gemini",
         "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions",
         "gemini-3.5-flash",
         true,
         "Google AI Studio (free tier, no card)",
         "GEMINI_API_KEY",
         {}},
        {"OpenRouter",
         "https://openrouter.ai/api/v1/chat/completions",
         "openrouter/free",
         true,
         "openrouter.ai/keys (free, no card; openrouter/free auto-picks a free model)",
         "OPENROUTER_API_KEY",
         {}},
        {"Ollama (local)",
         "http://localhost:11434/v1/chat/completions",
         "llama3.1",
         false,
         "no key: install ollama and run: ollama pull llama3.1",
         {},
         {}},
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

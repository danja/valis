// include/valis/AiProviders.h
//
// Named presets for the console's AI mode. The transport already speaks
// OpenAI-compatible chat completions against any endpoint, so a provider is
// just an endpoint plus a default model plus whether it needs a key. The
// Settings menu lists these; the custom endpoint/model/key items remain for
// anything not listed here.
//
// The list is an order, not a menu of one: the router walks it from the top
// when the chosen provider will not serve a request, so the entry best able to
// carry a whole Turtle document goes first.

#pragma once

#include "valis/RateLimits.h"

#include <string>
#include <vector>

namespace valis {
namespace ai {

/// One AI provider preset.
struct AiProvider
{
    std::string name;       ///< shown in the Settings menu, e.g. "Groq"
    std::string endpoint;   ///< chat-completions URL
    std::string model;      ///< default model id for the endpoint
    bool needsKey = true;   ///< false for keyless endpoints (local servers)
    std::string keyHint;    ///< where to get a key, shown when one is needed
    std::string keyEnvVar;  ///< environment variable consulted when nothing is stored

    /// What this provider calls its rate-limit headers. Empty names mean it
    /// reports nothing, in which case a 429 or a 413 is the only signal there
    /// will ever be.
    RateLimitHeaderNames headers;
};

/// Every known provider, in rotation order. Mistral first: it is the default,
/// and its measured token budget is seventy eight times Groq's, which matters
/// when every prompt carries a whole Turtle document.
const std::vector<AiProvider>& aiProviders();

/// The preset whose endpoint matches, or nullptr for a custom endpoint.
const AiProvider* findAiProvider(const std::string& endpoint);

}  // namespace ai
}  // namespace valis

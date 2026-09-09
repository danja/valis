// include/valis/AiProviders.h
//
// Named presets for the console's AI mode. The transport already speaks
// OpenAI-compatible chat completions against any endpoint, so a provider is
// just an endpoint plus a default model plus whether it needs a key. The
// Settings menu lists these; the custom endpoint/model/key items remain for
// anything not listed here.

#pragma once

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
};

/// Every known provider. Mistral first: it stays the default.
const std::vector<AiProvider>& aiProviders();

/// The preset whose endpoint matches, or nullptr for a custom endpoint.
const AiProvider* findAiProvider(const std::string& endpoint);

}  // namespace ai
}  // namespace valis

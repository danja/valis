// src/ai/AiRouter.h
//
// Rotation across providers. The endpoint list is an order, so a request the
// chosen provider will not serve moves down it rather than failing.
//
// Two things make this worth doing rather than retrying harder. A free tier's
// token budget is a ceiling on one request, not a queue that is reached
// eventually, so a circuit Groq can never accept is unremarkable to Mistral at
// seventy eight times the budget. And the response headers state the budget on
// every reply including the refusals, so the first refusal is enough to refuse
// the same request locally ever after, with a message that names the number.
//
// Classification is the whole game: rotating on a malformed request burns every
// free tier in turn to collect the same answer, and not rotating on a size
// refusal leaves the user waiting for a window that will never be wide enough.
// See include/valis/RateLimits.h and docs/rate-limit-advice.md.

#pragma once

#include "ai/ChatClient.h"
#include "valis/AiProviders.h"
#include "valis/ProviderBudgets.h"

#include <functional>
#include <string>
#include <vector>

namespace valis {
namespace ai {

/// How long a rolling window is worth waiting for rather than moving on. A
/// tight retry loop against a free tier is how an account earns a longer
/// cooldown, so this is the only place the router ever sleeps.
inline constexpr double kMaxWaitSeconds = 20.0;

struct RouteRequest
{
    std::string endpoint;      ///< the provider the user chose: tried first
    std::string model;         ///< the model for that endpoint
    std::string systemPrompt;
    std::string userPrompt;
};

/// The key for a provider, or empty when none is available. A provider that
/// needs a key and has none is skipped rather than asked.
using KeyLookup = std::function<std::string(const AiProvider&)>;

/// Pauses the calling thread. Injectable so tests never actually wait.
using Sleeper = std::function<void(double seconds)>;

struct RouteResult
{
    ChatResult result;
    std::string providerName;  ///< who answered, or who was asked last
    std::string endpoint;
    std::string model;
    /// One line per provider tried or skipped, in order, for the console to
    /// print when the request took more than the obvious path.
    std::vector<std::string> trace;
};

/// Sends `request`, falling through the provider order on 413, 429, 401/402/403
/// and 5xx, and never on 400. Requests known to exceed a provider's whole
/// budget are refused locally without a round-trip.
RouteResult route(const RouteRequest& request,
                  const KeyLookup& keys,
                  ProviderBudgets& budgets,
                  HttpPost transport = curlPost,
                  Sleeper sleeper = {});

}  // namespace ai
}  // namespace valis

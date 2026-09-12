// src/ai/AiRouter.cpp

#include "ai/AiRouter.h"

#include <chrono>
#include <thread>

namespace valis {
namespace ai {

namespace {

struct Candidate
{
    std::string name;
    std::string endpoint;
    std::string model;
    std::string apiKey;
    RateLimitHeaderNames headers;
};

std::string tokenText(long long tokens)
{
    return std::to_string(tokens) + " tokens";
}

/// The chosen endpoint first, then the preset order. A custom endpoint has no
/// preset, so it is carried as an entry of its own with no known header names.
std::vector<Candidate> buildOrder(const RouteRequest& request, const KeyLookup& keys,
                                  const ProviderBudgets& budgets,
                                  std::vector<std::string>& trace)
{
    std::vector<Candidate> order;

    AiProvider custom;
    custom.name = "the configured endpoint";
    custom.endpoint = request.endpoint;
    const auto* chosen = findAiProvider(request.endpoint);
    if (chosen == nullptr)
        chosen = &custom;

    // The chosen provider leads unless it has already refused to serve us at
    // all this session, in which case asking again costs a round-trip to be
    // told the same thing.
    std::string chosenReason;
    if (budgets.disabled(chosen->endpoint, chosenReason))
        trace.push_back("[" + chosen->name + " skipped: " + chosenReason + "]");
    else
        order.push_back({chosen->name, chosen->endpoint, request.model,
                         keys(*chosen), chosen->headers});

    for (const auto& provider : aiProviders())
    {
        if (provider.endpoint == request.endpoint)
            continue;

        const std::string key = keys(provider);
        if (provider.needsKey && key.empty())
            continue;  // no key: not a fallback, and not worth a round-trip

        std::string reason;
        if (budgets.disabled(provider.endpoint, reason))
        {
            trace.push_back("[" + provider.name + " skipped: " + reason + "]");
            continue;
        }

        order.push_back({provider.name, provider.endpoint, provider.model, key, provider.headers});
    }
    return order;
}

void defaultSleep(double seconds)
{
    if (seconds <= 0.0)
        return;
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0)));
}

}  // namespace

RouteResult route(const RouteRequest& request,
                  const KeyLookup& keys,
                  ProviderBudgets& budgets,
                  HttpPost transport,
                  Sleeper sleeper)
{
    RouteResult out;
    out.endpoint = request.endpoint;
    out.model = request.model;

    if (! sleeper)
        sleeper = defaultSleep;

    const auto order = buildOrder(request, keys, budgets, out.trace);
    const std::string prompt = request.systemPrompt + request.userPrompt;

    // The first provider that was merely out of room, kept for one retry after
    // the rest of the list has been tried. Waiting is worth a little when
    // nothing else will serve, and a tight retry loop against a free tier is
    // how an account earns a longer cooldown.
    const Candidate* retryCandidate = nullptr;
    double retryWait = 0.0;

    for (const auto& candidate : order)
    {
        out.providerName = candidate.name;
        out.endpoint = candidate.endpoint;
        out.model = candidate.model;

        const long long tokens = budgets.estimateFor(candidate.endpoint, prompt);
        const auto afford = budgets.afford(candidate.endpoint, tokens);

        if (afford.verdict == Affordability::Verdict::impossible)
        {
            // Measured once, refused ever after: this is the case where a
            // round-trip can only produce the same 413.
            out.trace.push_back("[" + candidate.name + " skipped: this request is about " +
                                tokenText(tokens) + " and its limit is " +
                                tokenText(afford.limitTokens) + "]");
            out.result = {};
            out.result.error = "this request is about " + tokenText(tokens) +
                               ", larger than " + candidate.name + "'s limit of " +
                               tokenText(afford.limitTokens) +
                               ". Waiting cannot help. Send a smaller circuit, or pick a "
                               "provider with a larger budget under Settings > AI Provider.";
            out.result.failure = Failure::tooLarge;
            continue;
        }

        if (afford.verdict == Affordability::Verdict::waitForReset)
        {
            const auto remaining = budgets.budget(candidate.endpoint).remainingTokens;
            out.trace.push_back("[" + candidate.name + " skipped: only " +
                                tokenText(remaining) + " left of its budget this minute]");
            out.result = {};
            out.result.error = candidate.name + " has only " + tokenText(remaining) +
                               " left this minute and this request needs about " +
                               tokenText(tokens) + ".";
            out.result.failure = Failure::rateLimited;
            if (retryCandidate == nullptr && afford.waitSeconds >= 0.0 &&
                afford.waitSeconds <= kMaxWaitSeconds)
            {
                retryCandidate = &candidate;
                retryWait = afford.waitSeconds;
            }
            continue;
        }

        auto attempt = chat(candidate.endpoint, candidate.apiKey, candidate.model,
                            request.systemPrompt, request.userPrompt, candidate.headers,
                            transport);

        // Headers arrive on failures too, and that is when they matter most.
        budgets.record(candidate.endpoint, attempt.budget);
        if (attempt.promptTokens > 0)
            budgets.observeUsage(candidate.endpoint, tokens, attempt.promptTokens);

        if (attempt.ok)
        {
            out.result = std::move(attempt);
            return out;
        }

        out.result = attempt;

        if (disablesProvider(attempt.failure))
            budgets.disable(candidate.endpoint, attempt.status == 402
                ? "the account cannot be billed (HTTP 402)"
                : "the key was rejected (HTTP " + std::to_string(attempt.status) + ")");

        if (! worthAnotherProvider(attempt.failure))
            return out;  // 400 and API-level errors: every provider says the same

        out.trace.push_back("[" + candidate.name + " could not serve this: " +
                            attempt.error + "]");

        if (retryCandidate == nullptr && attempt.failure == Failure::rateLimited)
        {
            // Mistral's 429 has an empty body and no retry-after, so a local
            // default is all there is.
            const double wait = attempt.budget.retryAfterSeconds >= 0.0
                ? attempt.budget.retryAfterSeconds
                : kMaxWaitSeconds;
            if (wait <= kMaxWaitSeconds)
            {
                retryCandidate = &candidate;
                retryWait = wait;
            }
        }
    }

    if (retryCandidate != nullptr)
    {
        out.trace.push_back("[waiting " + std::to_string(static_cast<int>(retryWait + 0.5)) +
                            "s and trying " + retryCandidate->name + " once more]");
        sleeper(retryWait);

        out.providerName = retryCandidate->name;
        out.endpoint = retryCandidate->endpoint;
        out.model = retryCandidate->model;

        const long long tokens = budgets.estimateFor(retryCandidate->endpoint, prompt);
        auto retry = chat(retryCandidate->endpoint, retryCandidate->apiKey,
                          retryCandidate->model, request.systemPrompt, request.userPrompt,
                          retryCandidate->headers, transport);
        budgets.record(retryCandidate->endpoint, retry.budget);
        if (retry.promptTokens > 0)
            budgets.observeUsage(retryCandidate->endpoint, tokens, retry.promptTokens);
        out.result = std::move(retry);
    }

    if (out.result.error.empty())
        out.result.error = "no AI provider is configured with a usable key - see Settings";
    return out;
}

}  // namespace ai
}  // namespace valis

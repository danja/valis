// include/valis/RateLimits.h
//
// Free-tier rate limits as providers actually report them, rather than as they
// document them. Three pure pieces, all measured against live endpoints (see
// docs/rate-limit-advice.md):
//
// - a token estimate, because the decision to send has to be made before the
//   provider is asked;
// - a status classifier, because "no room right now" and "bigger than the whole
//   budget" look alike and need opposite responses;
// - a response-header parser, because every reply, failures included, states
//   the budget that was applied.
//
// No JUCE, no I/O: the transport calls these, the tests call them directly.

#pragma once

#include <string>

namespace valis {
namespace ai {

// Measured tokens per character: 0.425 for code, 0.214 for English prose.
// Turtle is code, so the familiar "characters over four" (0.25) underestimates
// a circuit by about forty per cent, in the direction that causes failures.
// The code figure is rounded down to 0.42 so an estimate reads as a floor.
inline constexpr double kTokensPerCharCode  = 0.42;
inline constexpr double kTokensPerCharProse = 0.214;

enum class TextKind
{
    code,   ///< Turtle, JSON, source: dense in punctuation
    prose   ///< English sentences
};

/// A floor on how many tokens `text` will cost. Never negative.
long long estimateTokens(const std::string& text, TextKind kind = TextKind::code);

/// What a status means for the request that produced it. The distinction that
/// matters is between `tooLarge` (waiting cannot help, a bigger provider can)
/// and `rateLimited` (waiting can help), and between `badRequest` (every
/// provider will say the same) and `refused` (this provider will not serve us).
enum class Failure
{
    none,        ///< 2xx
    rateLimited, ///< 429: no room right now
    tooLarge,    ///< 413: bigger than this provider's whole budget
    refused,     ///< 401, 402, 403: this provider will not serve us at all
    fault,       ///< 408, 5xx: provider fault
    badRequest,  ///< 400: our request is wrong
    unknown      ///< anything else
};

Failure classifyStatus(int status);

/// Whether another provider is worth trying after `failure`. False for
/// `badRequest`: a malformed request is malformed everywhere, and rotating on
/// it burns every free tier in turn to collect the same answer.
bool worthAnotherProvider(Failure failure);

/// Whether `failure` means this provider should be left alone for the rest of
/// the session rather than merely skipped for this request.
bool disablesProvider(Failure failure);

/// Header names differ per provider: Groq says `x-ratelimit-limit-tokens`,
/// Mistral says `x-ratelimit-limit-tokens-minute`, and OpenRouter and Gemini
/// say nothing at all. An empty name means the provider does not report it.
struct RateLimitHeaderNames
{
    std::string limitTokens;
    std::string remainingTokens;
    std::string resetTokens;
    std::string limitRequests;
    std::string remainingRequests;
    std::string resetRequests;

    bool reportsAnything() const { return ! limitTokens.empty() || ! limitRequests.empty(); }
};

/// A provider's budget as last measured. Unknown stays distinguishable from
/// zero throughout: an unprobed provider must not look exhausted.
struct RateBudget
{
    long long limitTokens = -1;       ///< -1 unknown
    long long remainingTokens = -1;
    long long limitRequests = -1;
    long long remainingRequests = -1;
    double resetTokensSeconds = -1.0;
    double resetRequestsSeconds = -1.0;
    double retryAfterSeconds = -1.0;  ///< from the `retry-after` header

    bool limitKnown() const { return limitTokens > 0; }
};

/// Seconds out of the several shapes a reset field takes: a bare number of
/// seconds ("31"), a millisecond duration ("644ms"), or a compound duration
/// ("1m26.4s", "1h2m"). Matching only the leading unit of "1m26.4s" waits
/// twenty six seconds too little, every time.
bool parseDurationSeconds(const std::string& text, double& secondsOut);

/// Reads a raw HTTP response header block (as `curl -D` writes it) into a
/// budget. `retry-after` is read whatever the provider is called.
RateBudget parseRateLimitHeaders(const std::string& rawHeaders,
                                 const RateLimitHeaderNames& names);

/// Folds a fresh reading into a stored one. A reading that omits a field
/// leaves the stored value alone, and a reported limit of zero is treated as a
/// momentary symptom rather than a durable fact: one odd response must not
/// remove a provider for the session, and forgetting a known limit means
/// sending the same impossible request over and over.
void mergeBudget(RateBudget& stored, const RateBudget& fresh);

}  // namespace ai
}  // namespace valis

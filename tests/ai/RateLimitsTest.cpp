// tests/ai/RateLimitsTest.cpp
//
// The token estimate, the status classifier and the header parser are pure
// functions with no network. The header blocks below are the ones the real
// providers send, including their awkward shapes: Groq's duration strings,
// Mistral's window-in-the-name and missing reset, and a limit of zero.

#include "valis/ProviderBudgets.h"
#include "valis/RateLimits.h"

#include <cassert>
#include <cmath>
#include <string>

using namespace valis::ai;

namespace {

bool near(double a, double b)
{
    return std::fabs(a - b) < 1e-6;
}

RateLimitHeaderNames groqNames()
{
    return {"x-ratelimit-limit-tokens",       "x-ratelimit-remaining-tokens",
            "x-ratelimit-reset-tokens",       "x-ratelimit-limit-requests",
            "x-ratelimit-remaining-requests", "x-ratelimit-reset-requests"};
}

RateLimitHeaderNames mistralNames()
{
    return {"x-ratelimit-limit-tokens-minute", "x-ratelimit-remaining-tokens-minute",
            {},                                "x-ratelimit-limit-req-minute",
            "x-ratelimit-remaining-req-minute", {}};
}

}  // namespace

void testEstimateTokens()
{
    // Code is denser than prose: the same text costs about twice as much.
    const std::string turtle = ":vcf a val:Ladder ; val:cutoff 800.0 .";
    assert(estimateTokens(turtle) > estimateTokens(turtle, TextKind::prose));

    // The estimate is a floor, never negative, and scales with length.
    assert(estimateTokens("") == 0);
    assert(estimateTokens(std::string(1000, 'x')) == 420);
    assert(estimateTokens(std::string(1000, 'x'), TextKind::prose) == 214);

    // The figure that decides everything: a 40,000 character circuit is five
    // times Groq's measured 8,000 tokens per minute, so it can never be sent
    // there whole however long the console waits.
    assert(estimateTokens(std::string(40000, 'x')) > 8000);
}

void testClassifyStatus()
{
    assert(classifyStatus(200) == Failure::none);
    assert(classifyStatus(400) == Failure::badRequest);
    assert(classifyStatus(401) == Failure::refused);
    assert(classifyStatus(402) == Failure::refused);
    assert(classifyStatus(403) == Failure::refused);
    assert(classifyStatus(408) == Failure::fault);
    assert(classifyStatus(413) == Failure::tooLarge);
    assert(classifyStatus(429) == Failure::rateLimited);
    assert(classifyStatus(503) == Failure::fault);

    // 400 is ours to fix: every provider would say the same, so rotating on it
    // burns every free tier to collect one answer.
    assert(! worthAnotherProvider(Failure::badRequest));
    assert(worthAnotherProvider(Failure::tooLarge));
    assert(worthAnotherProvider(Failure::rateLimited));
    assert(worthAnotherProvider(Failure::refused));
    assert(worthAnotherProvider(Failure::fault));

    // Only a refusal to serve at all is worth remembering for the session.
    assert(disablesProvider(Failure::refused));
    assert(! disablesProvider(Failure::rateLimited));
    assert(! disablesProvider(Failure::fault));
}

void testParseDuration()
{
    double seconds = 0.0;
    assert(parseDurationSeconds("31", seconds) && near(seconds, 31.0));
    assert(parseDurationSeconds("644ms", seconds) && near(seconds, 0.644));
    assert(parseDurationSeconds("1.5s", seconds) && near(seconds, 1.5));
    // Matching only the leading unit here waits twenty six seconds too little.
    assert(parseDurationSeconds("1m26.4s", seconds) && near(seconds, 86.4));
    assert(parseDurationSeconds("2m", seconds) && near(seconds, 120.0));
    assert(parseDurationSeconds("1h2m3s", seconds) && near(seconds, 3723.0));
    assert(! parseDurationSeconds("", seconds));
    assert(! parseDurationSeconds("soon", seconds));
}

void testParseHeaders()
{
    const std::string groq =
        "HTTP/1.1 413 Payload Too Large\r\n"
        "retry-after: 31\r\n"
        "x-ratelimit-limit-tokens: 8000\r\n"
        "x-ratelimit-remaining-tokens: 8000\r\n"
        "x-ratelimit-reset-tokens: 644ms\r\n"
        "x-ratelimit-limit-requests: 1000\r\n"
        "x-ratelimit-reset-requests: 1m26.4s\r\n"
        "\r\n";

    const auto budget = parseRateLimitHeaders(groq, groqNames());
    assert(budget.limitTokens == 8000);
    // The budget is full: nothing was consumed because nothing was sent. A
    // size refusal, not a rate refusal.
    assert(budget.remainingTokens == 8000);
    assert(near(budget.resetTokensSeconds, 0.644));
    assert(near(budget.resetRequestsSeconds, 86.4));
    assert(near(budget.retryAfterSeconds, 31.0));
    assert(budget.remainingRequests == -1);  // absent stays unknown, not zero
    assert(budget.limitKnown());

    const std::string mistral =
        "HTTP/1.1 200 OK\r\n"
        "X-RateLimit-Limit-Tokens-Minute: 625000\r\n"
        "X-RateLimit-Remaining-Tokens-Minute: 624988\r\n"
        "x-ratelimit-limit-req-minute: 125\r\n"
        "\r\n";
    const auto second = parseRateLimitHeaders(mistral, mistralNames());
    assert(second.limitTokens == 625000);      // matched without case
    assert(second.remainingTokens == 624988);
    assert(second.limitRequests == 125);
    assert(second.resetTokensSeconds < 0.0);   // Mistral sends no reset at all

    // OpenRouter and Gemini report nothing: unknown must stay distinguishable
    // from zero, or an unprobed provider looks exhausted.
    const auto silent = parseRateLimitHeaders("HTTP/1.1 200 OK\r\n\r\n", {});
    assert(! silent.limitKnown());
    assert(silent.remainingTokens == -1);
}

void testMergeBudget()
{
    RateBudget stored;
    mergeBudget(stored, parseRateLimitHeaders(
        "x-ratelimit-limit-tokens: 8000\r\n"
        "x-ratelimit-remaining-tokens: 7914\r\n", groqNames()));
    assert(stored.limitTokens == 8000);

    // A rolled window restores the remaining budget; it does not make the
    // limit unknown. Forgetting it means sending the same impossible request
    // over and over.
    mergeBudget(stored, parseRateLimitHeaders(
        "x-ratelimit-remaining-tokens: 8000\r\n", groqNames()));
    assert(stored.limitTokens == 8000);
    assert(stored.remainingTokens == 8000);

    // A reported limit of zero is a momentary symptom, not a durable fact.
    mergeBudget(stored, parseRateLimitHeaders(
        "x-ratelimit-limit-tokens: 0\r\n"
        "x-ratelimit-remaining-tokens: 0\r\n", groqNames()));
    assert(stored.limitTokens == 8000);
    assert(stored.remainingTokens == 0);
}

void testAffordability()
{
    valis::ai::ProviderBudgets budgets;
    const std::string groq = "https://api.groq.com/openai/v1/chat/completions";

    // Nothing measured: send and let the provider answer.
    assert(budgets.afford(groq, 20000).verdict == Affordability::Verdict::unknown);

    budgets.record(groq, parseRateLimitHeaders(
        "x-ratelimit-limit-tokens: 8000\r\n"
        "x-ratelimit-remaining-tokens: 8000\r\n"
        "x-ratelimit-reset-tokens: 1ms\r\n", groqNames()));

    const auto impossible = budgets.afford(groq, 20229);
    assert(impossible.verdict == Affordability::Verdict::impossible);
    assert(impossible.limitTokens == 8000);

    assert(budgets.afford(groq, 4000).verdict == Affordability::Verdict::fits);

    budgets.record(groq, parseRateLimitHeaders(
        "x-ratelimit-remaining-tokens: 500\r\n"
        "x-ratelimit-reset-tokens: 12s\r\n", groqNames()));
    const auto wait = budgets.afford(groq, 4000);
    assert(wait.verdict == Affordability::Verdict::waitForReset);
    assert(near(wait.waitSeconds, 12.0));

    // A refusal is remembered for the session, and a new key forgets it.
    std::string reason;
    assert(! budgets.disabled(groq, reason));
    budgets.disable(groq, "the key was rejected (HTTP 401)");
    assert(budgets.disabled(groq, reason));
    assert(reason.find("401") != std::string::npos);
    budgets.clearDisabled();
    assert(! budgets.disabled(groq, reason));
}

void testEstimateCorrection()
{
    valis::ai::ProviderBudgets budgets;
    const std::string endpoint = "https://example.com/v1/chat/completions";
    const std::string text(1000, 'x');

    const long long raw = budgets.estimateFor(endpoint, text);
    assert(raw == estimateTokens(text));

    // The provider charged more than the estimate: later estimates follow it.
    budgets.observeUsage(endpoint, raw, raw * 2);
    assert(budgets.estimateFor(endpoint, text) > raw);

    // A cheaper reply must not make the estimate optimistic: it is a floor,
    // and a floor that drifts down stops protecting anything.
    valis::ai::ProviderBudgets other;
    other.observeUsage(endpoint, 1000, 100);
    assert(other.estimateFor(endpoint, text) == raw);
}

int main()
{
    testEstimateTokens();
    testClassifyStatus();
    testParseDuration();
    testParseHeaders();
    testMergeBudget();
    testAffordability();
    testEstimateCorrection();
    return 0;
}

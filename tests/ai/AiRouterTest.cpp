// tests/ai/AiRouterTest.cpp
//
// Rotation, with every provider stubbed. What is being checked is the
// classification: which failures move to the next provider, which stop the
// walk, and which are decided locally without a round-trip at all.

#include "ai/AiRouter.h"

#include <cassert>
#include <map>
#include <string>
#include <vector>

using namespace valis::ai;

namespace {

const std::string kMistral = "https://api.mistral.ai/v1/chat/completions";
const std::string kGroq    = "https://api.groq.com/openai/v1/chat/completions";

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

/// Answers each endpoint with a scripted response and records what it was asked.
struct Stub
{
    std::map<std::string, HttpResponse> answers;
    std::vector<std::string> asked;

    HttpPost transport()
    {
        return [this](const std::string& url, const std::string&, const std::string&,
                      HttpResponse& out, std::string& err)
        {
            asked.push_back(url);
            const auto found = answers.find(url);
            if (found == answers.end())
            {
                err = "unexpected endpoint " + url;
                return false;
            }
            out = found->second;
            return true;
        };
    }
};

HttpResponse reply(const std::string& text)
{
    return {200, R"({"choices":[{"message":{"content":")" + text + R"("}}]})", {}};
}

HttpResponse status(int code, const std::string& body, const std::string& headers = {})
{
    return {code, body, headers};
}

/// Every provider has a key, so nothing is skipped for want of one.
KeyLookup everyKey()
{
    return [](const AiProvider&) { return std::string("key"); };
}

RouteRequest request(const std::string& endpoint)
{
    return {endpoint, "some-model", "system prompt", "user prompt"};
}

}  // namespace

void testChosenProviderAnswers()
{
    Stub stub;
    stub.answers[kMistral] = reply("hello");

    ProviderBudgets budgets;
    const auto out = route(request(kMistral), everyKey(), budgets, stub.transport());

    assert(out.result.ok);
    assert(out.result.reply == "hello");
    assert(out.providerName == "Mistral");
    assert(stub.asked.size() == 1);
    assert(out.trace.empty());  // nothing to explain on the ordinary path
}

void testRotatesOnTooLarge()
{
    // Groq refuses on size. Waiting cannot fix that, but Mistral's budget is
    // seventy eight times larger, so the request goes through unchanged.
    Stub stub;
    stub.answers[kGroq] = status(413,
        R"json({"error":{"message":"Request too large on tokens per minute (TPM)"}})json",
        "x-ratelimit-limit-tokens: 8000\r\n");
    stub.answers[kMistral] = reply("designed it");

    ProviderBudgets budgets;
    const auto out = route(request(kGroq), everyKey(), budgets, stub.transport());

    assert(out.result.ok);
    assert(out.providerName == "Mistral");
    assert(stub.asked.size() == 2);
    assert(stub.asked[0] == kGroq);
    assert(! out.trace.empty());
    assert(contains(out.trace.front(), "Groq"));

    // The refusal taught the router Groq's limit, which is what makes the next
    // oversized request free to refuse.
    assert(budgets.budget(kGroq).limitTokens == 8000);
}

void testNeverRotatesOnBadRequest()
{
    // A malformed request is malformed everywhere. Walking the list would burn
    // every free tier in turn to collect the same answer.
    Stub stub;
    stub.answers[kMistral] = status(400, R"({"error":{"message":"model not found"}})");

    ProviderBudgets budgets;
    const auto out = route(request(kMistral), everyKey(), budgets, stub.transport());

    assert(! out.result.ok);
    assert(out.result.failure == Failure::badRequest);
    assert(stub.asked.size() == 1);
    assert(contains(out.result.error, "model not found"));
}

void testRefusalDisablesProviderForTheSession()
{
    // 402 is one account's billing problem. Asking again costs a round-trip to
    // be told the same thing, so the provider drops out until the key changes.
    Stub stub;
    stub.answers[kGroq] = status(402, R"({"message":"Payment required"})");
    stub.answers[kMistral] = reply("fine");

    ProviderBudgets budgets;
    const auto first = route(request(kGroq), everyKey(), budgets, stub.transport());
    assert(first.result.ok);

    std::string reason;
    assert(budgets.disabled(kGroq, reason));

    stub.asked.clear();
    const auto second = route(request(kGroq), everyKey(), budgets, stub.transport());
    assert(second.result.ok);
    assert(stub.asked.size() == 1);
    assert(stub.asked.front() == kMistral);  // Groq was not asked a second time

    budgets.clearDisabled();
    assert(! budgets.disabled(kGroq, reason));
}

void testImpossibleRequestIsRefusedWithoutSending()
{
    // Once the limit is known, a request larger than the whole budget is
    // refused locally: a round trip could only produce the same 413.
    ProviderBudgets budgets;
    budgets.record(kGroq, parseRateLimitHeaders(
        "x-ratelimit-limit-tokens: 8000\r\nx-ratelimit-remaining-tokens: 8000\r\n",
        {"x-ratelimit-limit-tokens", "x-ratelimit-remaining-tokens", {}, {}, {}, {}}));

    Stub stub;
    stub.answers[kMistral] = reply("still fine");

    auto big = request(kGroq);
    big.userPrompt = std::string(60000, 'x');  // about 25,000 tokens

    const auto out = route(big, everyKey(), budgets, stub.transport());
    assert(out.result.ok);
    assert(stub.asked.size() == 1);
    assert(stub.asked.front() == kMistral);
    assert(! out.trace.empty());
    assert(contains(out.trace.front(), "8000 tokens"));
}

void testKeylessProvidersAreNotAskedForFallback()
{
    // A provider with no key is not a fallback: asking costs a round-trip and
    // a 401 that teaches nothing.
    Stub stub;
    stub.answers[kMistral] = status(500, "");

    ProviderBudgets budgets;
    const KeyLookup onlyMistral = [](const AiProvider& provider)
    {
        return provider.endpoint == kMistral ? std::string("key") : std::string{};
    };

    const auto out = route(request(kMistral), onlyMistral, budgets, stub.transport(),
                           [](double) {});
    assert(! out.result.ok);
    // Only Mistral was reachable, and the local Ollama preset needs no key, so
    // it is the only other endpoint that may be tried.
    for (const auto& url : stub.asked)
        assert(url == kMistral || contains(url, "localhost"));
}

void testWaitsOnceForAShortWindow()
{
    // The last provider standing, rate limited with a short reset, is worth
    // one wait. Anything longer and moving on is faster.
    Stub stub;
    ProviderBudgets budgets;
    int calls = 0;
    double waited = 0.0;

    // Only Mistral answers at all, so it is the provider the router comes back
    // to after the rest of the list has been tried.
    HttpPost flaky = [&calls](const std::string& url, const std::string&, const std::string&,
                              HttpResponse& out, std::string& err)
    {
        if (url != kMistral)
        {
            err = "no route to host";
            return false;
        }
        out = ++calls == 1
            ? status(429, R"({"error":{"message":"Rate limit exceeded"}})", "retry-after: 2\r\n")
            : reply("second time lucky");
        return true;
    };

    const KeyLookup onlyMistral = [](const AiProvider& provider)
    {
        return provider.endpoint == kMistral ? std::string("key") : std::string{};
    };

    const auto out = route(request(kMistral), onlyMistral, budgets, flaky,
                           [&waited](double seconds) { waited += seconds; });
    assert(out.result.ok);
    assert(waited > 0.0 && waited <= kMaxWaitSeconds);
}

int main()
{
    testChosenProviderAnswers();
    testRotatesOnTooLarge();
    testNeverRotatesOnBadRequest();
    testRefusalDisablesProviderForTheSession();
    testImpossibleRequestIsRefusedWithoutSending();
    testKeylessProvidersAreNotAskedForFallback();
    testWaitsOnceForAShortWindow();
    return 0;
}

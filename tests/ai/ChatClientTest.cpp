// tests/ai/ChatClientTest.cpp
//
// The request builder and the reply parser are pure functions, tested here
// with no network. The transport is injected, so chat() is covered with a stub.

#include "ai/ChatClient.h"

#include <juce_core/juce_core.h>

#include <cassert>
#include <string>

using namespace valis::ai;

namespace {

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

void testBuildChatRequest()
{
    const auto json = buildChatRequest("mistral-small-latest", "be brief", "make a bass");
    const auto parsed = juce::JSON::parse(juce::String(json));
    assert(parsed.isObject());
    assert(parsed["model"].toString() == "mistral-small-latest");

    const auto messages = parsed["messages"];
    assert(messages.isArray() && messages.size() == 2);
    assert(messages[0]["role"].toString() == "system");
    assert(messages[0]["content"].toString() == "be brief");
    assert(messages[1]["role"].toString() == "user");
    assert(messages[1]["content"].toString() == "make a bass");
}

void testParseChatReply()
{
    std::string reply, error;

    const bool ok = parseChatReply(
        R"({"id":"x","choices":[{"message":{"role":"assistant","content":"Here:\n```turtle\n:a a val:Gain .\n```"},"finish_reason":"stop"}]})",
        reply, error);
    assert(ok);
    assert(contains(reply, "```turtle"));

    assert(! parseChatReply(R"({"error":{"message":"bad key","type":"auth"}})", reply, error));
    assert(contains(error, "bad key"));

    assert(! parseChatReply(R"({"object":"error","message":"Unauthorized"})", reply, error));
    assert(contains(error, "Unauthorized"));

    assert(! parseChatReply(R"({"choices":[]})", reply, error));
    assert(! parseChatReply("not json at all", reply, error));
}

void testFormatHttpError()
{
    // OpenAI shape inside a 429 with no "token" wording: treated as a
    // request-rate (RPM) limit, worth waiting out.
    const auto limited = formatHttpError(
        429, R"({"error":{"message":"Rate limit exceeded","type":"rate_limit"}})");
    assert(contains(limited, "429"));
    assert(contains(limited, "Rate limit exceeded"));
    assert(contains(limited, "wait"));

    // Mistral shape: {"object":"error","message":"..."}.
    const auto mistral = formatHttpError(
        429, R"({"object":"error","message":"Rate limit exceeded"})");
    assert(contains(mistral, "Rate limit exceeded"));

    // A 429 whose own message names tokens (TPM, as Groq and other
    // OpenAI-compatible providers report) gets different advice: waiting
    // does not help when a single request already exceeds the budget, so
    // the guidance is to shrink the request instead of to wait.
    const auto tokenLimited = formatHttpError(
        429, R"({"error":{"message":"Rate limit reached for model `x` on )"
             R"(tokens per minute (TPM): Limit 6000, Used 5500, Requested 700."}})");
    assert(contains(tokenLimited, "429"));
    assert(contains(tokenLimited, "tokens per minute"));
    assert(contains(tokenLimited, "shorter prompt"));
    assert(! contains(tokenLimited, "avoid rapid repeats"));

    // Auth failures point at the key, server failures at retrying later.
    assert(contains(formatHttpError(401, R"({"message":"Unauthorized"})"), "API key"));
    assert(contains(formatHttpError(500, ""), "try again later"));

    // A non-JSON body still reports the status rather than nothing.
    assert(contains(formatHttpError(503, "<html>down</html>"), "503"));

    // 413 is what Groq answers a per-minute token overflow with, and it is the
    // failure a user meets first: a big circuit plus a question. It gets the
    // token-limit advice, never the advice to wait.
    const auto tooLarge = formatHttpError(
        413, R"({"error":{"message":"Request too large for model `x` in organization )"
             R"(on tokens per minute (TPM): Limit 8000, Requested 13266"}})");
    assert(contains(tooLarge, "413"));
    assert(contains(tooLarge, "Limit 8000, Requested 13266"));
    assert(contains(tooLarge, "waiting cannot help"));
    assert(contains(tooLarge, "AI Provider"));
    assert(! contains(tooLarge, "wait a minute"));

    // 402 is one account's billing problem, not a malformed request: the
    // advice is to use a different provider, not to change the prompt.
    const auto unpaid = formatHttpError(402, R"({"message":"Payment required"})");
    assert(contains(unpaid, "Payment required"));
    assert(contains(unpaid, "AI Provider"));
}

void testParsePromptTokens()
{
    assert(parsePromptTokens(R"({"usage":{"prompt_tokens":1234,"total_tokens":1300}})") == 1234);
    assert(parsePromptTokens(R"({"choices":[]})") == -1);
    assert(parsePromptTokens("not json") == -1);
}

void testChatWithStubTransport()
{
    HttpPost stub = [](const std::string& url, const std::string& body,
                       const std::string& key, HttpResponse& out, std::string& err)
    {
        assert(contains(url, "mistral"));
        assert(contains(body, "mistral-small-latest"));
        assert(key == "secret");
        (void) err;
        out.status = 200;
        out.body = R"({"choices":[{"message":{"content":"hello"}}],)"
                   R"("usage":{"prompt_tokens":900}})";
        out.headers = "x-ratelimit-limit-tokens-minute: 625000\r\n"
                      "x-ratelimit-remaining-tokens-minute: 624100\r\n";
        return true;
    };

    const RateLimitHeaderNames mistral{"x-ratelimit-limit-tokens-minute",
                                       "x-ratelimit-remaining-tokens-minute", {},
                                       "x-ratelimit-limit-req-minute", {}, {}};

    const auto ok = chat("https://api.mistral.ai/v1/chat/completions", "secret",
                         "mistral-small-latest", "sys", "hi", mistral, stub);
    assert(ok.ok);
    assert(ok.reply == "hello");
    // The budget rides back on the reply, so the next request can be sized
    // before it is sent.
    assert(ok.budget.limitTokens == 625000);
    assert(ok.promptTokens == 900);
    assert(ok.failure == Failure::none);

    // A 413 is a result, not a transport failure: its headers state the budget
    // and its status says a bigger provider is the answer.
    HttpPost refusing = [](const std::string&, const std::string&, const std::string&,
                           HttpResponse& out, std::string&)
    {
        out.status = 413;
        out.body = R"json({"error":{"message":"Request too large on tokens per minute (TPM)"}})json";
        out.headers = "retry-after: 31\r\nx-ratelimit-limit-tokens: 8000\r\n";
        return true;
    };
    const RateLimitHeaderNames groq{"x-ratelimit-limit-tokens", {}, {}, {}, {}, {}};
    const auto refused = chat("https://api.groq.com/openai/v1/chat/completions", "k",
                              "openai/gpt-oss-20b", "sys", "hi", groq, refusing);
    assert(! refused.ok);
    assert(refused.status == 413);
    assert(refused.failure == Failure::tooLarge);
    assert(refused.budget.limitTokens == 8000);
    assert(contains(refused.error, "waiting cannot help"));

    HttpPost failing = [](const std::string&, const std::string&, const std::string&,
                          HttpResponse&, std::string& err)
    {
        err = "no route to host";
        return false;
    };
    const auto failed = chat("https://api.mistral.ai/v1/chat/completions", "secret",
                             "mistral-small-latest", "sys", "hi", {}, failing);
    assert(! failed.ok);
    assert(contains(failed.error, "no route to host"));
    // Unreachable is not our mistake: another provider is worth trying.
    assert(worthAnotherProvider(failed.failure));

    const auto unconfigured = chat("", "", "", "sys", "hi", {}, stub);
    assert(! unconfigured.ok);
    assert(! worthAnotherProvider(unconfigured.failure));
}

int main()
{
    testBuildChatRequest();
    testParseChatReply();
    testFormatHttpError();
    testParsePromptTokens();
    testChatWithStubTransport();
    return 0;
}

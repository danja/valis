// tests/ai/MistralClientTest.cpp
//
// The request builder and the reply parser are pure functions, tested here
// with no network. The transport is injected, so chat() is covered with a stub.

#include "ai/MistralClient.h"

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

void testChatWithStubTransport()
{
    HttpPost stub = [](const std::string& url, const std::string& body,
                       const std::string& key, std::string& out, std::string& err)
    {
        assert(contains(url, "mistral"));
        assert(contains(body, "mistral-small-latest"));
        assert(key == "secret");
        (void) err;
        out = R"({"choices":[{"message":{"content":"hello"}}]})";
        return true;
    };

    const auto ok = chat("https://api.mistral.ai/v1/chat/completions", "secret",
                         "mistral-small-latest", "sys", "hi", stub);
    assert(ok.ok);
    assert(ok.reply == "hello");

    HttpPost failing = [](const std::string&, const std::string&, const std::string&,
                          std::string&, std::string& err)
    {
        err = "no route to host";
        return false;
    };
    const auto failed = chat("https://api.mistral.ai/v1/chat/completions", "secret",
                             "mistral-small-latest", "sys", "hi", failing);
    assert(! failed.ok);
    assert(contains(failed.error, "no route to host"));

    const auto unconfigured = chat("", "", "", "sys", "hi", stub);
    assert(! unconfigured.ok);
}

int main()
{
    testBuildChatRequest();
    testParseChatReply();
    testChatWithStubTransport();
    return 0;
}

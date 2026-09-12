// src/ai/MistralClient.h
//
// A minimal client for an OpenAI-compatible chat-completions endpoint - the
// Mistral API (https://api.mistral.ai/v1/chat/completions) by default, any
// compatible URL by configuration.
//
// Why a curl subprocess rather than a linked HTTP client: the plugin builds
// with JUCE_USE_CURL=0, under which JUCE's native Linux networking is plain
// HTTP with no TLS, and adding a TLS library would be a new dependency for an
// experimental feature. The curl command-line tool speaks HTTPS already, so
// one short-lived child process per request keeps the build dependency-free.
// The request body travels via a temporary file and the process runs on a
// background thread owned by the console view - never on the audio thread,
// and never more than one in flight.
//
// Only used by the plugin target; the console's command surface and prompt
// live in valis_core and are tested without any of this.

#pragma once

#include <functional>
#include <string>

namespace valis {
namespace ai {

/// Posts `body` to `url` and captures the response. Returns true with
/// `responseOut` set on HTTP 2xx (even for an API-level error payload, which
/// the caller parses); false with `errorOut` set when the request never
/// completed or the status is not 2xx.
using HttpPost = std::function<bool(const std::string& url,
                                    const std::string& body,
                                    const std::string& apiKey,
                                    std::string& responseOut,
                                    std::string& errorOut)>;

/// Builds the {"model":..,"messages":[system,user]} request document.
std::string buildChatRequest(const std::string& model,
                             const std::string& systemPrompt,
                             const std::string& userPrompt);

/// Reads choices[0].message.content out of a chat-completions reply, or an
/// API-level {"error":...} payload. Returns true with `replyOut` set on
/// success.
bool parseChatReply(const std::string& responseJson,
                    std::string& replyOut,
                    std::string& errorOut);

/// Turns a non-2xx HTTP status plus the response body into an actionable
/// error: the server's message when the body holds one, plus what to do
/// about the status (check the key on 401, retry later on 5xx, ...). A 429
/// is split by what the server's own message says was exhausted: a request
/// (RPM) limit is worth waiting out, but a token (TPM) limit can reject a
/// single oversized request outright, so the advice there is to shrink the
/// prompt or circuit instead. Pure and unit-tested; the transport calls it,
/// the parser never sees error pages.
std::string formatHttpError(int status, const std::string& body);

/// The default transport: one `curl` child process, body via a temporary file.
bool curlPost(const std::string& url,
              const std::string& body,
              const std::string& apiKey,
              std::string& responseOut,
              std::string& errorOut);

struct ChatResult
{
    bool ok = false;
    std::string reply;   ///< the assistant's text, when ok
    std::string error;   ///< why not, otherwise
};

/// One synchronous round-trip. Runs wherever the caller runs it - the console
/// view calls this on a background thread. `transport` is injectable so tests
/// never touch the network.
ChatResult chat(const std::string& endpoint,
               const std::string& apiKey,
               const std::string& model,
               const std::string& systemPrompt,
               const std::string& userPrompt,
               HttpPost transport = curlPost);

}  // namespace ai
}  // namespace valis

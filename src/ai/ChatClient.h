// src/ai/ChatClient.h
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
// Response headers are captured as well as the body: they carry the provider's
// rate limits, they arrive on failures too, and without them the size of the
// budget can only be guessed at. See include/valis/RateLimits.h.
//
// Only used by the plugin target; the console's command surface and prompt
// live in valis_core and are tested without any of this.

#pragma once

#include "valis/RateLimits.h"

#include <functional>
#include <string>

namespace valis {
namespace ai {

/// One HTTP round-trip's result, whatever the status. A non-2xx status is not
/// a transport failure: the body and headers are still worth reading, because
/// that is where the provider says what it would have accepted.
struct HttpResponse
{
    int status = 0;          ///< 0 when the request never reached a server
    std::string body;
    std::string headers;     ///< raw response header block
};

/// Posts `body` to `url`. Returns false only when the request never completed
/// at all (no curl, no route, timeout), with `errorOut` set; any HTTP status,
/// including 4xx and 5xx, returns true with `responseOut` filled in.
using HttpPost = std::function<bool(const std::string& url,
                                    const std::string& body,
                                    const std::string& apiKey,
                                    HttpResponse& responseOut,
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

/// `usage.prompt_tokens` from a reply, or -1 when it is absent. The local
/// token estimate corrects itself against this.
long long parsePromptTokens(const std::string& responseJson);

/// Turns a non-2xx HTTP status plus the response body into an actionable
/// error: the server's message when the body holds one, plus what to do
/// about the status. The two rate-limit shapes get opposite advice. A request
/// (RPM) limit is worth waiting out. A token (TPM) limit, reported either as a
/// 429 whose message names tokens or as a bare 413, can reject a single
/// oversized request outright, and no amount of waiting will ever make that
/// request fit, so the advice there is to shrink it or move to a provider with
/// a larger budget. Pure and unit-tested; the transport calls it, the parser
/// never sees error pages.
std::string formatHttpError(int status, const std::string& body);

/// The default transport: one `curl` child process, body via a temporary file,
/// response headers via a second one.
bool curlPost(const std::string& url,
              const std::string& body,
              const std::string& apiKey,
              HttpResponse& responseOut,
              std::string& errorOut);

struct ChatResult
{
    bool ok = false;
    std::string reply;   ///< the assistant's text, when ok
    std::string error;   ///< why not, otherwise
    int status = 0;      ///< the HTTP status, when there was one
    Failure failure = Failure::none;
    RateBudget budget;   ///< whatever the response headers stated
    long long promptTokens = -1;  ///< what the provider charged for the prompt
};

/// One synchronous round-trip against one endpoint. Runs wherever the caller
/// runs it - the console view calls this on a background thread. `transport`
/// is injectable so tests never touch the network. `headers` names the
/// provider's rate-limit headers so the reply's budget is read back.
ChatResult chat(const std::string& endpoint,
                const std::string& apiKey,
                const std::string& model,
                const std::string& systemPrompt,
                const std::string& userPrompt,
                const RateLimitHeaderNames& headers = {},
                HttpPost transport = curlPost);

}  // namespace ai
}  // namespace valis

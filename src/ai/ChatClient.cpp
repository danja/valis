// src/ai/ChatClient.cpp

#include "ai/ChatClient.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace valis {
namespace ai {

std::string buildChatRequest(const std::string& model,
                             const std::string& systemPrompt,
                             const std::string& userPrompt)
{
    auto* system = new juce::DynamicObject();
    system->setProperty("role", "system");
    system->setProperty("content", juce::String(systemPrompt));

    auto* user = new juce::DynamicObject();
    user->setProperty("role", "user");
    user->setProperty("content", juce::String(userPrompt));

    juce::Array<juce::var> messages;
    messages.add(juce::var(system));
    messages.add(juce::var(user));

    auto* request = new juce::DynamicObject();
    request->setProperty("model", juce::String(model));
    request->setProperty("messages", messages);

    return juce::JSON::toString(juce::var(request), false).toStdString();
}

bool parseChatReply(const std::string& responseJson,
                    std::string& replyOut,
                    std::string& errorOut)
{
    const auto parsed = juce::JSON::parse(juce::String(responseJson));
    if (! parsed.isObject())
    {
        errorOut = "the model returned something that is not JSON";
        return false;
    }

    if (const auto apiError = parsed["error"]; ! apiError.isVoid())
    {
        // OpenAI shape: {"error":{"message":...}} or {"error":"..."}.
        const juce::String message = apiError.isObject()
            ? apiError["message"].toString()
            : apiError.toString();
        errorOut = "the API refused the request: " + message.toStdString();
        return false;
    }

    // Mistral shape: {"object":"error","message":"..."} with no choices.
    if (const auto message = parsed["message"]; message.isString())
    {
        errorOut = "the API refused the request: " + message.toString().toStdString();
        return false;
    }

    const auto choices = parsed["choices"];
    if (! choices.isArray() || choices.size() < 1)
    {
        errorOut = "the API reply held no choices";
        return false;
    }

    const juce::String content = choices[0]["message"]["content"].toString();
    if (content.isEmpty())
    {
        errorOut = "the API reply held an empty message";
        return false;
    }

    replyOut = content.toStdString();
    return true;
}

long long parsePromptTokens(const std::string& responseJson)
{
    const auto parsed = juce::JSON::parse(juce::String(responseJson));
    if (! parsed.isObject())
        return -1;
    const auto usage = parsed["usage"];
    if (! usage.isObject())
        return -1;
    const auto tokens = usage["prompt_tokens"];
    if (! tokens.isInt() && ! tokens.isInt64() && ! tokens.isDouble())
        return -1;
    const auto value = static_cast<long long>(static_cast<juce::int64>(tokens));
    return value > 0 ? value : -1;
}

/// Best-effort pull of the server's message out of an error body, which is
/// usually {"error":{"message":...}} or {"message":"..."}.
std::string errorBodyMessage(const std::string& body)
{
    const auto parsed = juce::JSON::parse(juce::String(body));
    if (! parsed.isObject())
        return {};
    if (const auto apiError = parsed["error"]; ! apiError.isVoid())
        return (apiError.isObject() ? apiError["message"].toString()
                                    : apiError.toString()).toStdString();
    if (const auto message = parsed["message"]; message.isString())
        return message.toString().toStdString();
    return {};
}

namespace {

/// The advice for a budget that one request cannot fit inside, whatever status
/// carried it. Waiting is not part of it: the ceiling is on a single request,
/// not on a queue that is reached eventually.
const char* tooLargeAdvice()
{
    return "The request is larger than this provider's per-minute token "
           "budget, so waiting cannot help: the limit caps one request, not a "
           "queue. Send a smaller circuit or a shorter prompt, or switch to a "
           "provider with a larger budget under Settings > AI Provider "
           "(Mistral's measured budget is far larger than Groq's).";
}

}  // namespace

std::string formatHttpError(int status, const std::string& body)
{
    const std::string detail = errorBodyMessage(body);
    std::string out = "HTTP " + std::to_string(status);
    out += detail.empty() ? ": the API refused the request"
                          : ": " + detail;

    if (status == 413)
    {
        // Groq answers a per-minute token overflow with 413 rather than 429,
        // and reports the budget as untouched on the way out: nothing was
        // consumed because nothing was sent. It is a size refusal, not a rate
        // refusal, and advice about waiting cannot work.
        out += " - " + std::string(tooLargeAdvice());
    }
    else if (status == 429)
    {
        // The server's own wording says which budget was exhausted. A token
        // (TPM) limit can reject a single oversized request outright, so the
        // "wait it out" advice that fits a request (RPM) limit is actively
        // wrong there: shrinking the request is what actually helps.
        std::string lower = detail;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("token") != std::string::npos)
            out += " - rate limited on tokens per minute. " + std::string(tooLargeAdvice());
        else
            out += " - rate limited. Free-tier keys allow very few requests "
                   "per minute, so wait a minute before retrying and avoid "
                   "rapid repeats.";
        out += " Check the provider's own usage dashboard for its current "
               "limits and any outage status.";
    }
    else if (status == 402)
        out += " - this account cannot be billed, so the provider will not "
               "serve any request on this key. Pick another provider under "
               "Settings > AI Provider.";
    else if (status == 401 || status == 403)
        out += " - check the API key under Settings";
    else if (status >= 500)
        out += " - the server failed; try again later";
    return out;
}

bool curlPost(const std::string& url,
              const std::string& body,
              const std::string& apiKey,
              HttpResponse& responseOut,
              std::string& errorOut)
{
    responseOut = {};

    juce::TemporaryFile bodyFile("valis-ai-request");
    if (const auto stream = bodyFile.getFile().createOutputStream())
    {
        if (! stream->writeText(juce::String(body), false, false, "\n"))
        {
            errorOut = "could not write the request to a temporary file";
            return false;
        }
    }
    else
    {
        errorOut = "could not write the request to a temporary file";
        return false;
    }

    // The response headers go to their own file rather than into stdout: they
    // carry the rate-limit budget, and mixing them with the JSON body would
    // mean splitting one stream two different ways.
    juce::TemporaryFile headerFile("valis-ai-headers");

    juce::StringArray args;
    args.add("curl");
    args.add("-sS");           // silent, but show errors
    args.add("-m");
    args.add("120");
    args.add("-X");
    args.add("POST");
    args.add(url);
    args.add("-H");
    args.add("Content-Type: application/json");
    if (! apiKey.empty())
    {
        args.add("-H");
        args.add("Authorization: Bearer " + juce::String(apiKey));
    }
    args.add("--data-binary");
    args.add("@" + bodyFile.getFile().getFullPathName());
    args.add("-D");
    args.add(headerFile.getFile().getFullPathName());
    // Append the status on its own marked line: without --fail curl exits 0
    // for HTTP error pages, and the body alone cannot tell a 429 from a 200
    // that carries an error payload. Args pass through with no shell, so the
    // %{...} placeholder reaches curl verbatim.
    args.add("-w");
    args.add("\nVALIS_HTTP_CODE:%{http_code}\n");

    juce::ChildProcess curl;
    if (! curl.start(args, juce::ChildProcess::wantStdOut))
    {
        errorOut = "could not start curl - is it installed?";
        return false;
    }

    // waitForProcessToFinish returns what readProcessOutput would: the child's
    // stdout. A non-zero exit still leaves the body (often empty) to report.
    if (! curl.waitForProcessToFinish(125000))
    {
        errorOut = "timed out waiting for curl";
        return false;
    }
    responseOut.body = curl.readAllProcessOutput().toStdString();
    const int exitCode = curl.getExitCode();
    if (exitCode != 0)
    {
        errorOut = "curl exited with code " + std::to_string(exitCode) +
                   (responseOut.body.empty() ? "" : ": " + responseOut.body);
        responseOut = {};
        return false;
    }

    responseOut.headers = headerFile.getFile().loadFileAsString().toStdString();

    // Split off curl's marked status trailer. It is only honoured at the very
    // end of the output, so a body that happens to mention the marker is
    // left alone. When the trailer is missing (an unexpected curl build),
    // fall through to the old body-only behaviour.
    constexpr const char* kCodeMarker = "\nVALIS_HTTP_CODE:";
    if (const auto marker = responseOut.body.rfind(kCodeMarker); marker != std::string::npos)
    {
        const std::string tail = responseOut.body.substr(marker + 17);
        const int status = std::atoi(tail.c_str());
        const bool isTrailer = status >= 100 && status <= 599 && tail.size() >= 3 &&
            std::all_of(tail.begin() + 3, tail.end(), [](char c)
            {
                return c == '\n' || c == '\r' || c == ' ' || c == '\t';
            });
        if (isTrailer)
        {
            responseOut.body.erase(marker);
            responseOut.status = status;
        }
    }
    if (responseOut.status == 0 && responseOut.body.empty())
    {
        errorOut = "the server returned an empty response";
        return false;
    }
    if (responseOut.status == 0)
        responseOut.status = 200;  // no trailer: treat a body as a reply, as before
    return true;
}

ChatResult chat(const std::string& endpoint,
                const std::string& apiKey,
                const std::string& model,
                const std::string& systemPrompt,
                const std::string& userPrompt,
                const RateLimitHeaderNames& headers,
                HttpPost transport)
{
    ChatResult result;

    if (endpoint.empty() || model.empty())
    {
        result.error = "no AI endpoint or model is configured - see Settings";
        result.failure = Failure::badRequest;
        return result;
    }

    HttpResponse response;
    std::string transportError;
    if (! transport(endpoint, buildChatRequest(model, systemPrompt, userPrompt),
                    apiKey, response, transportError))
    {
        result.error = transportError.empty() ? "the request failed" : transportError;
        // The request never reached a server. Another provider may still be
        // reachable, so this counts as a fault rather than as our mistake.
        result.failure = Failure::fault;
        return result;
    }

    result.status = response.status;
    result.budget = parseRateLimitHeaders(response.headers, headers);
    result.failure = classifyStatus(response.status);

    if (result.failure != Failure::none)
    {
        result.error = formatHttpError(response.status, response.body);
        return result;
    }

    if (response.body.empty())
    {
        result.error = "the server returned an empty response";
        result.failure = Failure::fault;
        return result;
    }

    result.promptTokens = parsePromptTokens(response.body);

    if (! parseChatReply(response.body, result.reply, result.error))
    {
        // A 2xx carrying an API-level error payload: the request reached the
        // model and came back wrong, which no other provider would fix.
        result.failure = Failure::badRequest;
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace ai
}  // namespace valis

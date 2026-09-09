// src/ai/MistralClient.cpp

#include "ai/MistralClient.h"

#include <juce_core/juce_core.h>

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

bool curlPost(const std::string& url,
              const std::string& body,
              const std::string& apiKey,
              std::string& responseOut,
              std::string& errorOut)
{
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
    responseOut = curl.readAllProcessOutput().toStdString();
    const int exitCode = curl.getExitCode();
    if (exitCode != 0)
    {
        errorOut = "curl exited with code " + std::to_string(exitCode) +
                   (responseOut.empty() ? "" : ": " + responseOut);
        responseOut.clear();
        return false;
    }
    if (responseOut.empty())
    {
        errorOut = "the server returned an empty response";
        return false;
    }
    return true;
}

ChatResult chat(const std::string& endpoint,
               const std::string& apiKey,
               const std::string& model,
               const std::string& systemPrompt,
               const std::string& userPrompt,
               HttpPost transport)
{
    ChatResult result;

    if (endpoint.empty() || model.empty())
    {
        result.error = "no AI endpoint or model is configured - see Settings";
        return result;
    }

    std::string response, transportError;
    if (! transport(endpoint, buildChatRequest(model, systemPrompt, userPrompt),
                    apiKey, response, transportError))
    {
        result.error = transportError.empty() ? "the request failed" : transportError;
        return result;
    }

    if (! parseChatReply(response, result.reply, result.error))
        return result;

    result.ok = true;
    return result;
}

}  // namespace ai
}  // namespace valis

// src/ai/RateLimits.cpp

#include "valis/RateLimits.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace valis {
namespace ai {

namespace {

std::string toLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/// Pulls one header value out of a raw header block. Names are matched without
/// case, as HTTP requires; the last occurrence wins, which is what a redirect
/// chain writes into one `-D` file.
bool headerValue(const std::string& rawHeaders, const std::string& name, std::string& valueOut)
{
    if (name.empty())
        return false;

    const std::string wanted = toLower(name);
    bool found = false;
    std::istringstream lines(rawHeaders);
    std::string line;
    while (std::getline(lines, line))
    {
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        if (toLower(trim(line.substr(0, colon))) != wanted)
            continue;
        valueOut = trim(line.substr(colon + 1));
        found = true;
    }
    return found;
}

bool headerNumber(const std::string& rawHeaders, const std::string& name, long long& valueOut)
{
    std::string text;
    if (! headerValue(rawHeaders, name, text) || text.empty())
        return false;
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str())
        return false;
    valueOut = parsed;
    return true;
}

bool headerDuration(const std::string& rawHeaders, const std::string& name, double& secondsOut)
{
    std::string text;
    if (! headerValue(rawHeaders, name, text))
        return false;
    return parseDurationSeconds(text, secondsOut);
}

}  // namespace

long long estimateTokens(const std::string& text, TextKind kind)
{
    const double rate = (kind == TextKind::prose) ? kTokensPerCharProse : kTokensPerCharCode;
    return static_cast<long long>(std::ceil(static_cast<double>(text.size()) * rate));
}

Failure classifyStatus(int status)
{
    if (status >= 200 && status < 300)
        return Failure::none;
    switch (status)
    {
        case 400: return Failure::badRequest;
        case 401:
        case 402:
        case 403: return Failure::refused;
        case 408: return Failure::fault;
        case 413: return Failure::tooLarge;
        case 429: return Failure::rateLimited;
        default: break;
    }
    if (status >= 500)
        return Failure::fault;
    return Failure::unknown;
}

bool worthAnotherProvider(Failure failure)
{
    switch (failure)
    {
        case Failure::rateLimited:
        case Failure::tooLarge:
        case Failure::refused:
        case Failure::fault:
            return true;
        case Failure::none:
        case Failure::badRequest:
        case Failure::unknown:
        default:
            return false;
    }
}

bool disablesProvider(Failure failure)
{
    // A key that is missing, rejected or unpaid stays that way for the session:
    // asking again costs a round-trip and gets the same answer. A rate limit or
    // a fault is momentary and the provider is worth revisiting.
    return failure == Failure::refused;
}

bool parseDurationSeconds(const std::string& text, double& secondsOut)
{
    const std::string value = toLower(trim(text));
    if (value.empty())
        return false;

    double total = 0.0;
    bool any = false;
    std::size_t i = 0;
    while (i < value.size())
    {
        if (std::isspace(static_cast<unsigned char>(value[i])) != 0)
        {
            ++i;
            continue;
        }

        const char* start = value.c_str() + i;
        char* end = nullptr;
        const double number = std::strtod(start, &end);
        if (end == start)
            return any;  // trailing junk after a good prefix is ignored
        i += static_cast<std::size_t>(end - start);

        // The unit follows the number. "ms" must be tested before "m", or
        // "644ms" reads as 644 minutes.
        if (value.compare(i, 2, "ms") == 0)
        {
            total += number / 1000.0;
            i += 2;
        }
        else if (i < value.size() && value[i] == 'h')
        {
            total += number * 3600.0;
            ++i;
        }
        else if (i < value.size() && value[i] == 'm')
        {
            total += number * 60.0;
            ++i;
        }
        else if (i < value.size() && value[i] == 's')
        {
            total += number;
            ++i;
        }
        else
        {
            total += number;  // bare number: seconds, as retry-after sends
        }
        any = true;
    }

    if (! any)
        return false;
    secondsOut = total;
    return true;
}

RateBudget parseRateLimitHeaders(const std::string& rawHeaders,
                                 const RateLimitHeaderNames& names)
{
    RateBudget budget;
    headerNumber(rawHeaders, names.limitTokens, budget.limitTokens);
    headerNumber(rawHeaders, names.remainingTokens, budget.remainingTokens);
    headerNumber(rawHeaders, names.limitRequests, budget.limitRequests);
    headerNumber(rawHeaders, names.remainingRequests, budget.remainingRequests);
    headerDuration(rawHeaders, names.resetTokens, budget.resetTokensSeconds);
    headerDuration(rawHeaders, names.resetRequests, budget.resetRequestsSeconds);
    headerDuration(rawHeaders, "retry-after", budget.retryAfterSeconds);
    return budget;
}

void mergeBudget(RateBudget& stored, const RateBudget& fresh)
{
    // A limit of zero is a symptom, not a fact: Mistral has reported
    // `x-ratelimit-limit-req-minute: 0` hours after reporting 125.
    if (fresh.limitTokens > 0)
        stored.limitTokens = fresh.limitTokens;
    if (fresh.limitRequests > 0)
        stored.limitRequests = fresh.limitRequests;

    // Remaining counts may legitimately be zero, and only a fresh reading
    // says anything about them.
    if (fresh.remainingTokens >= 0)
        stored.remainingTokens = fresh.remainingTokens;
    if (fresh.remainingRequests >= 0)
        stored.remainingRequests = fresh.remainingRequests;
    if (fresh.resetTokensSeconds >= 0.0)
        stored.resetTokensSeconds = fresh.resetTokensSeconds;
    if (fresh.resetRequestsSeconds >= 0.0)
        stored.resetRequestsSeconds = fresh.resetRequestsSeconds;
    if (fresh.retryAfterSeconds >= 0.0)
        stored.retryAfterSeconds = fresh.retryAfterSeconds;
}

}  // namespace ai
}  // namespace valis

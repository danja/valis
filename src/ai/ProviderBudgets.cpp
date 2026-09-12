// src/ai/ProviderBudgets.cpp

#include "valis/ProviderBudgets.h"

#include <algorithm>
#include <cmath>

namespace valis {
namespace ai {

ProviderBudgets& ProviderBudgets::instance()
{
    static ProviderBudgets budgets;
    return budgets;
}

void ProviderBudgets::record(const std::string& endpoint, const RateBudget& fresh)
{
    const std::lock_guard<std::mutex> guard(lock);
    mergeBudget(entries[endpoint].budget, fresh);
}

void ProviderBudgets::disable(const std::string& endpoint, const std::string& reason)
{
    const std::lock_guard<std::mutex> guard(lock);
    auto& entry = entries[endpoint];
    entry.disabled = true;
    entry.disabledReason = reason;
}

bool ProviderBudgets::disabled(const std::string& endpoint, std::string& reasonOut) const
{
    const std::lock_guard<std::mutex> guard(lock);
    const auto found = entries.find(endpoint);
    if (found == entries.end() || ! found->second.disabled)
        return false;
    reasonOut = found->second.disabledReason;
    return true;
}

void ProviderBudgets::clearDisabled()
{
    const std::lock_guard<std::mutex> guard(lock);
    for (auto& entry : entries)
    {
        entry.second.disabled = false;
        entry.second.disabledReason.clear();
    }
}

RateBudget ProviderBudgets::budget(const std::string& endpoint) const
{
    const std::lock_guard<std::mutex> guard(lock);
    const auto found = entries.find(endpoint);
    return found == entries.end() ? RateBudget{} : found->second.budget;
}

void ProviderBudgets::observeUsage(const std::string& endpoint, long long estimated,
                                   long long actual)
{
    if (estimated <= 0 || actual <= 0)
        return;

    const std::lock_guard<std::mutex> guard(lock);
    auto& entry = entries[endpoint];
    const double ratio = static_cast<double>(actual) / static_cast<double>(estimated);
    // Exponential smoothing, weighted towards early readings so the correction
    // settles quickly and then stops moving on one unusual prompt.
    const double weight = entry.observations == 0 ? 1.0 : 0.25;
    entry.estimateRatio = entry.estimateRatio * (1.0 - weight) + ratio * weight;
    // A correction that would make the estimate optimistic is clamped away:
    // the estimate must stay a floor, or the pre-flight check stops protecting
    // anything.
    entry.estimateRatio = std::max(entry.estimateRatio, 1.0);
    ++entry.observations;
}

long long ProviderBudgets::estimateFor(const std::string& endpoint, const std::string& text,
                                       TextKind kind) const
{
    const long long raw = estimateTokens(text, kind);
    const std::lock_guard<std::mutex> guard(lock);
    const auto found = entries.find(endpoint);
    if (found == entries.end() || found->second.observations == 0)
        return raw;
    return static_cast<long long>(std::ceil(static_cast<double>(raw) * found->second.estimateRatio));
}

Affordability ProviderBudgets::afford(const std::string& endpoint, long long tokens) const
{
    Affordability out;
    out.estimatedTokens = tokens;

    const std::lock_guard<std::mutex> guard(lock);
    const auto found = entries.find(endpoint);
    if (found == entries.end() || ! found->second.budget.limitKnown())
        return out;  // unknown: send and let the provider answer

    const auto& budget = found->second.budget;
    out.limitTokens = budget.limitTokens;
    if (tokens > budget.limitTokens)
    {
        // No amount of waiting moves a ceiling on a single request.
        out.verdict = Affordability::Verdict::impossible;
        return out;
    }
    if (budget.remainingTokens >= 0 && tokens > budget.remainingTokens)
    {
        out.verdict = Affordability::Verdict::waitForReset;
        out.waitSeconds = budget.resetTokensSeconds;
        return out;
    }
    out.verdict = Affordability::Verdict::fits;
    return out;
}

}  // namespace ai
}  // namespace valis

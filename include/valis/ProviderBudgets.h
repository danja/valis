// include/valis/ProviderBudgets.h
//
// What has been learned about each endpoint this session: its token budget,
// how much of it is left, whether it refused to serve us at all, and how far
// the local token estimate was out last time.
//
// The headers arrive on every response including the failures, so the first
// refusal is enough to know a provider's limit - and knowing it means the same
// impossible request is never sent to that provider twice.
//
// Written from the AI worker thread and read from the message thread, so every
// access takes a short mutex. Nothing here is on the audio path.

#pragma once

#include "valis/RateLimits.h"

#include <map>
#include <mutex>
#include <string>

namespace valis {
namespace ai {

/// The verdict of a pre-flight check against a known budget.
struct Affordability
{
    enum class Verdict
    {
        unknown,      ///< nothing measured yet: send and find out
        fits,         ///< within the budget that is left
        waitForReset, ///< the whole budget covers it, but not what is left now
        impossible    ///< larger than this provider's entire per-minute budget
    };

    Verdict verdict = Verdict::unknown;
    long long estimatedTokens = 0;
    long long limitTokens = -1;
    double waitSeconds = -1.0;
};

class ProviderBudgets
{
public:
    ProviderBudgets() = default;

    /// The instance the console and the router share. Tests build their own.
    static ProviderBudgets& instance();

    /// Folds a response's headers into what is known about `endpoint`.
    void record(const std::string& endpoint, const RateBudget& fresh);

    /// Marks `endpoint` as one that will not serve us this session (401, 402,
    /// 403). Cleared when the user changes the key.
    void disable(const std::string& endpoint, const std::string& reason);
    bool disabled(const std::string& endpoint, std::string& reasonOut) const;

    /// Forgets every refusal, for when the key or endpoint changes.
    void clearDisabled();

    RateBudget budget(const std::string& endpoint) const;

    /// Corrects the local estimate against `usage.prompt_tokens` from a real
    /// reply. The estimate is a fixed characters-to-tokens rate, which is
    /// within a few per cent for Turtle but drifts for other content; one
    /// smoothed ratio per endpoint removes the systematic part of the error.
    void observeUsage(const std::string& endpoint, long long estimated, long long actual);

    /// `estimateTokens` corrected by what this endpoint has actually charged.
    long long estimateFor(const std::string& endpoint, const std::string& text,
                          TextKind kind = TextKind::code) const;

    /// Whether a request of `tokens` can be sent to `endpoint` now.
    Affordability afford(const std::string& endpoint, long long tokens) const;

private:
    struct Entry
    {
        RateBudget budget;
        bool disabled = false;
        std::string disabledReason;
        double estimateRatio = 1.0;  ///< actual / estimated, smoothed
        int observations = 0;
    };

    mutable std::mutex lock;
    std::map<std::string, Entry> entries;
};

}  // namespace ai
}  // namespace valis

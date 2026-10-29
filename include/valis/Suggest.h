// include/valis/Suggest.h
//
// "Did you mean ...?" for an error about a name that is not there. A name spelt almost right
// is the commonest mistake a person or a program makes with exact, case-sensitive symbols, and
// an error that offers the nearest real ones saves a round trip.

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace valis {

/// How many single-letter changes turn one name into the other, ignoring case.
inline std::size_t editDistance(std::string_view a, std::string_view b)
{
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j)
        row[j] = j;

    for (std::size_t i = 1; i <= a.size(); ++i)
    {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j)
        {
            const std::size_t above = row[j];
            const bool same = std::tolower(static_cast<unsigned char>(a[i - 1])) ==
                              std::tolower(static_cast<unsigned char>(b[j - 1]));
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (same ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

/// The few names nearest to `wanted`: close in spelling, or one containing the other. Best first,
/// at most five. Nothing is returned for a name that is nothing like any of them.
inline std::vector<std::string> closestNames(const std::string& wanted, const std::vector<std::string>& candidates)
{
    const auto lower = [](std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
        return text;
    };

    const std::size_t limit = std::max<std::size_t>(2, wanted.size() / 3);
    const std::string wantedLower = lower(wanted);

    std::vector<std::pair<std::size_t, std::string>> scored;
    for (const auto& candidate : candidates)
    {
        const std::string other = lower(candidate);
        std::size_t distance = editDistance(wanted, candidate);
        if (! wantedLower.empty() && (other.find(wantedLower) != std::string::npos ||
                                      wantedLower.find(other) != std::string::npos))
            distance = std::min<std::size_t>(distance, 1);   // a name inside another is a near miss

        if (distance <= limit)
            scored.emplace_back(distance, candidate);
    }

    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::string> names;
    for (std::size_t i = 0; i < scored.size() && i < 5; ++i)
        names.push_back(scored[i].second);
    return names;
}

/// " Did you mean val:Ladder, val:Tanh or val:Gain?" or nothing. `prefix` goes before each name
/// and `quote` around it: val:Ladder, or 'cutoff'.
inline std::string didYouMean(const std::string& prefix, const std::vector<std::string>& names,
                              const std::string& quote = {})
{
    if (names.empty())
        return {};

    std::string text = " Did you mean ";
    for (std::size_t i = 0; i < names.size(); ++i)
        text += (i == 0 ? "" : (i + 1 == names.size() ? " or " : ", ")) + quote + prefix + names[i] + quote;
    return text + "?";
}

/// "in, cutoff and resonance": a list for an error to show what is there.
inline std::string listNames(const std::vector<std::string>& names)
{
    std::string text;
    for (std::size_t i = 0; i < names.size(); ++i)
        text += (i == 0 ? "" : (i + 1 == names.size() ? " and " : ", ")) + names[i];
    return text;
}

}  // namespace valis

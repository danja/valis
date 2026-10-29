// src/dsp/Tuning.h
//
// A Scala (.scl) tuning: the pitches of one period of a scale, as cents above
// the root, repeating at the last one. Shared by val:MidiPitch, which turns a
// note into a frequency, and val:Quantise, which snaps a pitch to the scale.
// Format: https://www.huygens-fokker.org/scala/scl_format.html
//
// Parsing allocates, so it happens on the message thread. Lookups do not.

#pragma once

#include "dsp/ResolveFile.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace valis::dsp {

class Tuning
{
public:
    static constexpr std::size_t kMaxDegrees = 128;

    bool empty() const noexcept { return cents.empty(); }
    std::size_t size() const noexcept { return cents.size(); }
    const std::string& description() const noexcept { return text; }

    /// Cents above the root of scale degree `degree`, which may be negative or
    /// past the period: degree 0 is the root, and each lap adds the period.
    double centsOfDegree(long degree) const noexcept
    {
        if (cents.empty())
            return static_cast<double>(degree) * 100.0;

        const long n = static_cast<long>(cents.size());
        long lap = degree / n;
        long step = degree % n;
        if (step < 0) { step += n; --lap; }

        return static_cast<double>(lap) * cents.back()
             + (step == 0 ? 0.0 : cents[static_cast<std::size_t>(step - 1)]);
    }

    /// The frequency of MIDI `note` when `rootKey` sounds the 12-tone pitch
    /// from an A of `referenceHz`, and the scale is counted up from the root.
    /// An empty tuning is twelve-tone equal temperament.
    double frequency(int note, int rootKey, double referenceHz) const noexcept
    {
        const double rootHz = referenceHz * std::exp2(static_cast<double>(rootKey - 69) / 12.0);
        return rootHz * std::exp2(centsOfDegree(note - rootKey) / 1200.0);
    }

    /// The scale degree's pitch nearest `semitones` from the root, in
    /// semitones. Halfway between two, the lower wins.
    double nearestSemitones(double semitones) const noexcept
    {
        if (cents.empty())
            return std::round(semitones);

        const double target = semitones * 100.0;
        const long   n      = static_cast<long>(cents.size());
        const long   lap    = static_cast<long>(std::floor(target / cents.back()));

        double best = 0.0, bestDistance = 1.0e30;
        for (long step = 0; step <= n; ++step)
        {
            const double candidate = centsOfDegree(lap * n + step);
            const double distance  = std::abs(candidate - target);
            if (distance < bestDistance - 1.0e-9)
            {
                bestDistance = distance;
                best = candidate;
            }
        }
        return best / 100.0;
    }

    /// Reads the text of an .scl file. On failure the tuning is left empty and
    /// `error` says which line was wrong.
    bool parse(std::string_view source, std::string& error)
    {
        cents.clear();
        text.clear();

        std::vector<std::string> lines;
        std::size_t start = 0;
        while (start <= source.size())
        {
            auto end = source.find('\n', start);
            if (end == std::string_view::npos) end = source.size();
            auto line = source.substr(start, end - start);
            if (! line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (line.empty() || line.front() != '!')
                lines.emplace_back(line);
            start = end + 1;
        }

        // The description may be blank, so the first line is kept even when
        // empty; every line after it must carry something.
        if (lines.size() < 2)
            return fail(error, "no note count after the description");

        text = trim(lines[0]);

        char* stop = nullptr;
        const auto countText = trim(lines[1]);
        const long count = std::strtol(countText.c_str(), &stop, 10);
        if (countText.empty() || stop == countText.c_str() || count < 1)
            return fail(error, "expected a positive note count, got '" + countText + "'");
        if (count > static_cast<long>(kMaxDegrees))
            return fail(error, "more than " + std::to_string(kMaxDegrees) + " notes in a period");

        std::vector<double> parsed;
        for (std::size_t i = 2; i < lines.size() && static_cast<long>(parsed.size()) < count; ++i)
        {
            const auto line = trim(lines[i]);
            if (line.empty())
                continue;

            double value = 0.0;
            if (! parsePitch(line, value))
                return fail(error, "cannot read the pitch '" + line + "'");
            parsed.push_back(value);
        }

        if (static_cast<long>(parsed.size()) != count)
            return fail(error, "promised " + std::to_string(count) + " notes, found "
                                   + std::to_string(parsed.size()));

        if (! (parsed.front() > 0.0) || ! std::is_sorted(parsed.begin(), parsed.end()))
            return fail(error, "pitches must rise from above the root to the period");

        cents = std::move(parsed);
        return true;
    }

    /// Reads a tuning file, found as val:file paths are.
    bool load(std::string_view path, std::string& error)
    {
        const auto file = resolveFile(path);
        if (! file.existsAsFile())
            return fail(error, "not found: " + file.getFileName().toStdString());

        if (parse(file.loadFileAsString().toStdString(), error))
            return true;

        error = file.getFileName().toStdString() + ": " + error;
        return false;
    }

private:
    static std::string trim(const std::string& s)
    {
        const auto a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return {};
        const auto b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    }

    /// A pitch with a decimal point is cents; anything else is a ratio, "n" or
    /// "n/d". Text after the first word is a comment.
    static bool parsePitch(const std::string& line, double& cents)
    {
        const auto word = line.substr(0, line.find_first_of(" \t"));
        char* stop = nullptr;

        if (word.find('.') != std::string::npos)
        {
            cents = std::strtod(word.c_str(), &stop);
            return stop == word.c_str() + word.size();
        }

        const auto slash = word.find('/');
        const double top = std::strtod(word.c_str(), &stop);
        if (stop == word.c_str() || (slash == std::string::npos ? *stop != '\0' : stop != word.c_str() + slash))
            return false;

        double bottom = 1.0;
        if (slash != std::string::npos)
        {
            const auto rest = word.substr(slash + 1);
            bottom = std::strtod(rest.c_str(), &stop);
            if (rest.empty() || stop != rest.c_str() + rest.size())
                return false;
        }

        if (! (top > 0.0) || ! (bottom > 0.0))
            return false;

        cents = 1200.0 * std::log2(top / bottom);
        return true;
    }

    bool fail(std::string& error, std::string reason)
    {
        cents.clear();
        error = std::move(reason);
        return false;
    }

    std::vector<double> cents;   ///< degrees 1..n above the root; the last is the period
    std::string         text;
};

}  // namespace valis::dsp

// src/shot/SectionCompare.cpp

#include "valis/SectionCompare.h"

#include "valis/SectionMap.h"
#include "valis/SpectralDistance.h"
#include "valis/StereoImage.h"

#include <algorithm>
#include <cmath>

namespace valis {

namespace {

constexpr double kMinSliceSeconds = 1.0;

std::vector<float> mixDown(const std::vector<float>& left, const std::vector<float>& right)
{
    std::vector<float> m = left;
    if (right.size() == left.size())
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (left[i] + right[i]);
    return m;
}

std::vector<float> slice(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    to = std::min(to, x.size());
    return from < to ? std::vector<float>(x.begin() + static_cast<std::ptrdiff_t>(from), x.begin() + static_cast<std::ptrdiff_t>(to))
                     : std::vector<float>();
}

double rmsDb(const std::vector<float>& x)
{
    double squares = 0.0;
    for (const float s : x) squares += static_cast<double>(s) * s;
    return 20.0 * std::log10(std::max(std::sqrt(squares / static_cast<double>(std::max<std::size_t>(x.size(), 1))), 1.0e-6));
}

/// Mean width of the valid seconds in [from, to), or false when there are none.
bool meanWidth(const StereoImage& image, std::size_t from, std::size_t to, double& out)
{
    double sum = 0.0;
    long used = 0;
    for (std::size_t s = from; s < to && s < image.seconds.size(); ++s)
        if (image.seconds[s].valid)
        {
            sum += image.seconds[s].widthDb;
            ++used;
        }
    if (used == 0)
        return false;
    out = sum / static_cast<double>(used);
    return true;
}

}  // namespace

SectionComparison compareSections(const std::vector<float>& leftA, const std::vector<float>& rightA,
                                  const std::vector<float>& leftB, const std::vector<float>& rightB, double sampleRate)
{
    SectionComparison result;
    if (sampleRate <= 0.0 || leftA.empty() || leftB.empty())
        return result;

    const auto monoA = mixDown(leftA, rightA), monoB = mixDown(leftB, rightB);
    const bool stereo = rightA.size() == leftA.size() && rightB.size() == leftB.size();
    const StereoImage imageA = stereo ? analyseStereoImage(leftA, rightA, sampleRate) : StereoImage();
    const StereoImage imageB = stereo ? analyseStereoImage(leftB, rightB, sampleRate) : StereoImage();

    const auto map = findSections(monoA, sampleRate, nullptr);
    const auto perSecond = static_cast<std::size_t>(sampleRate);

    for (const auto& section : map.sections)
    {
        const auto from = static_cast<std::size_t>(section.startSeconds), to = static_cast<std::size_t>(section.endSeconds);
        const auto a = slice(monoA, from * perSecond, to * perSecond);
        const auto b = slice(monoB, from * perSecond, to * perSecond);
        if (static_cast<double>(b.size()) < kMinSliceSeconds * sampleRate)
            continue;

        SectionDifference d;
        d.startSeconds = section.startSeconds;
        d.endSeconds = section.endSeconds;
        d.spectralDistanceDb = spectralDistance(a, b, sampleRate).meanDb;
        d.levelDifferenceDb = rmsDb(b) - rmsDb(a);
        double widthA = 0.0, widthB = 0.0;
        if (stereo && meanWidth(imageA, from, to, widthA) && meanWidth(imageB, from, to, widthB))
        {
            d.hasWidth = true;
            d.widthDifferenceDb = widthB - widthA;
        }
        result.sections.push_back(d);
    }

    for (std::size_t i = 0; i < result.sections.size(); ++i)
        if (result.mostChanged < 0 || result.sections[i].spectralDistanceDb
                                          > result.sections[static_cast<std::size_t>(result.mostChanged)].spectralDistanceDb)
            result.mostChanged = static_cast<int>(i);
    return result;
}

}  // namespace valis

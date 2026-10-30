// src/shot/EnvelopeTimes.cpp

#include "valis/EnvelopeTimes.h"

#include <algorithm>
#include <cmath>

namespace valis {

namespace {

/// RMS envelope with one value per millisecond, each over a centred window of `windowMs`. Frames of 1 ms
/// would dip at every zero crossing below 500 Hz, so the window is longer than the hop.
std::vector<double> smoothEnvelope(const std::vector<float>& x, double rate, double windowMs)
{
    const std::size_t n = x.size();
    const std::size_t hop = std::max<std::size_t>(1, static_cast<std::size_t>(rate / 1000.0));
    const std::size_t half = std::max<std::size_t>(1, static_cast<std::size_t>(windowMs * rate / 2000.0));
    std::vector<double> cumulative(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        cumulative[i + 1] = cumulative[i] + static_cast<double>(x[i]) * x[i];
    std::vector<double> out;
    for (std::size_t start = 0; start < n; start += hop)
    {
        const std::size_t mid = start + hop / 2;
        const std::size_t a = mid > half ? mid - half : 0, b = std::min(n, mid + half);
        out.push_back(std::sqrt((cumulative[b] - cumulative[a]) / static_cast<double>(std::max<std::size_t>(b - a, 1)) + 1.0e-20));
    }
    return out;
}

double toDb(double v) { return 20.0 * std::log10(std::max(v, 1.0e-6)); }

}  // namespace

EnvelopeTimes measureEnvelope(const std::vector<float>& mono, double rate)
{
    EnvelopeTimes r;
    if (mono.size() < static_cast<std::size_t>(rate * 0.02) || rate <= 0.0)
        return r;

    const auto e5 = smoothEnvelope(mono, rate, 5.0);
    const auto e10 = smoothEnvelope(mono, rate, 10.0);

    const double loudest5 = *std::max_element(e5.begin(), e5.end());
    std::size_t i0 = 0;
    while (i0 < e5.size() && e5[i0] < loudest5 * 0.1)
        ++i0;
    r.attackAtMs = static_cast<double>(i0);
    double localMax = 0.0;
    const std::size_t localEnd = std::min(e5.size(), i0 + 300);
    for (std::size_t i = i0; i < localEnd; ++i)
        localMax = std::max(localMax, e5[i]);
    if (localEnd > i0)
    {
        std::size_t k = i0;
        while (k < localEnd && e5[k] < 0.9 * localMax)
            ++k;
        r.attackMs = static_cast<double>(k - i0);
    }

    const std::size_t ip = static_cast<std::size_t>(std::max_element(e10.begin(), e10.end()) - e10.begin());
    r.peakAtMs = static_cast<double>(ip);
    const double threshold = e10[ip] * std::pow(10.0, -30.0 / 20.0);
    for (std::size_t i = ip; i < e10.size(); ++i)
    {
        double ahead = 0.0;                                   // the level must stay down for the next 20 ms
        for (std::size_t j = i; j < std::min(e10.size(), i + 21); ++j)
            ahead = std::max(ahead, e10[j]);
        if (ahead < threshold)
        {
            r.decayMs = static_cast<double>(i - ip);
            break;
        }
    }

    const std::size_t frame = static_cast<std::size_t>(rate * 0.02);
    std::vector<double> rms;
    for (std::size_t s = 0; s + frame <= mono.size(); s += frame)
    {
        double sum = 0.0;
        for (std::size_t i = s; i < s + frame; ++i)
            sum += static_cast<double>(mono[i]) * mono[i];
        rms.push_back(std::sqrt(sum / static_cast<double>(frame) + 1.0e-20));
    }
    if (! rms.empty())
    {
        auto sorted = rms;
        std::sort(sorted.begin(), sorted.end());
        r.noiseFloorDbfs = toDb(sorted[static_cast<std::size_t>(0.1 * static_cast<double>(sorted.size() - 1))]);
        r.dynamicRangeDb = toDb(sorted.back()) - r.noiseFloorDbfs;
    }
    return r;
}

}  // namespace valis

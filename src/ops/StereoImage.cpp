// src/ops/StereoImage.cpp

#include "valis/StereoImage.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace valis {

namespace {

constexpr int kOrder = 11;
constexpr int kFrame = 1 << kOrder;   // 2048 samples
constexpr int kHop = kFrame / 2;

constexpr double kMinMidRms = 1.0e-4; // about -80 dBFS: no usable signal
constexpr double kBassTop = 250.0;
constexpr double kTrebleBottom = 4000.0;

double median(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double ratioDb(double side, double mid) { return 10.0 * std::log10((side + 1.0e-12) / (mid + 1.0e-12)); }

std::string oneDecimal(double v)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

std::string twoDecimals(double v)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}

}  // namespace

StereoImage analyseStereoImage(const std::vector<float>& left, const std::vector<float>& right, double sampleRate)
{
    StereoImage image;
    const std::size_t n = std::min(left.size(), right.size());
    if (sampleRate <= 0.0 || n == 0)
        return image;

    const std::size_t perSecond = static_cast<std::size_t>(sampleRate);
    const std::size_t count = perSecond > 0 ? n / perSecond : 0;
    if (count == 0)
        return image;

    juce::dsp::FFT fft(kOrder);
    juce::dsp::WindowingFunction<float> window(kFrame, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> midBuffer(kFrame * 2), sideBuffer(kFrame * 2);

    // The bin that starts each band; a bin's frequency is bin * rate / frame.
    const double binHz = sampleRate / kFrame;
    const int bassEnd = static_cast<int>(std::ceil(kBassTop / binHz));
    const int trebleStart = static_cast<int>(std::ceil(kTrebleBottom / binHz));

    image.seconds.resize(count);
    for (std::size_t s = 0; s < count; ++s)
    {
        const std::size_t begin = s * perSecond;
        double midSquares = 0.0, sideSquares = 0.0, sumL = 0.0, sumR = 0.0;
        for (std::size_t i = 0; i < perSecond; ++i)
        {
            const double l = left[begin + i], r = right[begin + i];
            const double m = 0.5 * (l + r), d = 0.5 * (l - r);
            midSquares += m * m;
            sideSquares += d * d;
            sumL += l;
            sumR += r;
        }
        const double rate = static_cast<double>(perSecond);
        const double midRms = std::sqrt(midSquares / rate);
        const double sideRms = std::sqrt(sideSquares / rate);
        if (! std::isfinite(midRms) || ! std::isfinite(sideRms) || midRms < kMinMidRms)
            continue;

        auto& out = image.seconds[s];
        out.valid = true;
        out.widthDb = 20.0 * std::log10((sideRms + 1.0e-9) / (midRms + 1.0e-9));

        // Correlation, with the mean taken out. A channel that does not move has none to
        // report, which reads as 1 here, the same as identical channels.
        const double meanL = sumL / rate, meanR = sumR / rate;
        double ll = 0.0, rr = 0.0, lr = 0.0;
        for (std::size_t i = 0; i < perSecond; ++i)
        {
            const double l = left[begin + i] - meanL, r = right[begin + i] - meanR;
            ll += l * l;
            rr += r * r;
            lr += l * r;
        }
        out.correlation = ll > 1.0e-6 * rate && rr > 1.0e-6 * rate ? lr / std::sqrt(ll * rr) : 1.0;

        // Width by band, from the power of whole frames that lie inside this second.
        double mid[3] = {0, 0, 0}, side[3] = {0, 0, 0};
        for (std::size_t at = begin; at + kFrame <= begin + perSecond; at += kHop)
        {
            std::fill(midBuffer.begin(), midBuffer.end(), 0.0f);
            std::fill(sideBuffer.begin(), sideBuffer.end(), 0.0f);
            for (std::size_t i = 0; i < static_cast<std::size_t>(kFrame); ++i)
            {
                midBuffer[i] = 0.5f * (left[at + i] + right[at + i]);
                sideBuffer[i] = 0.5f * (left[at + i] - right[at + i]);
            }
            window.multiplyWithWindowingTable(midBuffer.data(), kFrame);
            window.multiplyWithWindowingTable(sideBuffer.data(), kFrame);
            fft.performFrequencyOnlyForwardTransform(midBuffer.data());
            fft.performFrequencyOnlyForwardTransform(sideBuffer.data());
            for (int b = 1; b < kFrame / 2; ++b)
            {
                const int band = b < bassEnd ? 0 : (b < trebleStart ? 1 : 2);
                const double m = midBuffer[static_cast<std::size_t>(b)], d = sideBuffer[static_cast<std::size_t>(b)];
                mid[band] += m * m;
                side[band] += d * d;
            }
        }
        out.lowDb = ratioDb(side[0], mid[0]);
        out.midDb = ratioDb(side[1], mid[1]);
        out.highDb = ratioDb(side[2], mid[2]);
    }

    std::vector<double> widths, correlations;
    for (const auto& sec : image.seconds)
    {
        if (! sec.valid)
            continue;
        ++image.validSeconds;
        widths.push_back(sec.widthDb);
        correlations.push_back(sec.correlation);
        image.lowDb += sec.lowDb;
        image.midDb += sec.midDb;
        image.highDb += sec.highDb;
    }
    if (image.validSeconds == 0)
        return image;

    const double valid = static_cast<double>(image.validSeconds);
    image.lowDb /= valid;
    image.midDb /= valid;
    image.highDb /= valid;
    image.medianWidthDb = median(widths);
    image.medianCorrelation = median(correlations);

    // Ten-second windows against the track's own median.
    for (std::size_t from = 0; from < count; from += 10)
    {
        const std::size_t to = std::min(from + 10, count);
        double w = 0.0, c = 0.0;
        long used = 0;
        for (std::size_t s = from; s < to; ++s)
        {
            if (! image.seconds[s].valid)
                continue;
            w += image.seconds[s].widthDb;
            c += image.seconds[s].correlation;
            ++used;
        }
        if (used == 0)
            continue;
        w /= static_cast<double>(used);
        c /= static_cast<double>(used);

        std::string reason;
        if (std::abs(w - image.medianWidthDb) >= 4.0)
            reason = "width " + oneDecimal(w) + " dB against a track median of " + oneDecimal(image.medianWidthDb) + " dB";
        if (c <= image.medianCorrelation - 0.35)
            reason += std::string(reason.empty() ? "" : ", ") + "correlation " + twoDecimals(c)
                    + " against a track median of " + twoDecimals(image.medianCorrelation);
        if (! reason.empty())
            image.anomalies.push_back({static_cast<double>(from), static_cast<double>(to), reason});
    }
    return image;
}

}  // namespace valis

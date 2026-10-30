// src/shot/SpectralDistance.cpp

#include "valis/SpectralDistance.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace valis {

namespace {

double hzToMel(double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); }
double melToHz(double mel) { return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0); }

/// Triangular mel filters over the bins of a frame of `frame` samples: one row of weights per band.
std::vector<std::vector<float>> melBank(int frame, int bands, double rate)
{
    const int bins = frame / 2 + 1;
    const double top = hzToMel(rate / 2.0);
    std::vector<double> edges(static_cast<std::size_t>(bands) + 2);
    for (int i = 0; i < bands + 2; ++i)
        edges[static_cast<std::size_t>(i)] = melToHz(top * i / (bands + 1)) * frame / rate;   // in bins
    std::vector<std::vector<float>> bank(static_cast<std::size_t>(bands), std::vector<float>(static_cast<std::size_t>(bins), 0.0f));
    for (int b = 0; b < bands; ++b)
    {
        const double lo = edges[static_cast<std::size_t>(b)], mid = edges[static_cast<std::size_t>(b) + 1], hi = edges[static_cast<std::size_t>(b) + 2];
        for (int k = 0; k < bins; ++k)
        {
            double w = 0.0;
            if (k > lo && k <= mid)      w = (k - lo) / std::max(mid - lo, 1.0e-9);
            else if (k > mid && k < hi)  w = (hi - k) / std::max(hi - mid, 1.0e-9);
            bank[static_cast<std::size_t>(b)][static_cast<std::size_t>(k)] = static_cast<float>(w);
        }
    }
    return bank;
}

/// Log mel power, band by frame.
std::vector<std::vector<float>> melSpectrogram(const std::vector<float>& x, int frame, int bands, double rate)
{
    int order = 0;
    while ((1 << order) < frame)
        ++order;
    juce::dsp::FFT fft(order);
    const int hop = frame / 4;
    const auto bank = melBank(frame, bands, rate);
    std::vector<float> window(static_cast<std::size_t>(frame));
    for (int i = 0; i < frame; ++i)
        window[static_cast<std::size_t>(i)] = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(i) / static_cast<float>(frame));

    std::vector<std::vector<float>> out(static_cast<std::size_t>(bands));
    std::vector<float> buffer(static_cast<std::size_t>(2 * frame));
    for (long start = -frame / 2; start < static_cast<long>(x.size()); start += hop)
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        for (int i = 0; i < frame; ++i)
        {
            const long j = start + i;
            if (j >= 0 && j < static_cast<long>(x.size()))
                buffer[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(j)] * window[static_cast<std::size_t>(i)];
        }
        fft.performRealOnlyForwardTransform(buffer.data(), true);
        std::vector<float> power(static_cast<std::size_t>(frame / 2 + 1));
        for (int k = 0; k <= frame / 2; ++k)
        {
            const float re = buffer[static_cast<std::size_t>(2 * k)], im = buffer[static_cast<std::size_t>(2 * k) + 1];
            power[static_cast<std::size_t>(k)] = re * re + im * im;
        }
        for (int b = 0; b < bands; ++b)
        {
            double sum = 0.0;
            for (int k = 0; k <= frame / 2; ++k)
                sum += static_cast<double>(bank[static_cast<std::size_t>(b)][static_cast<std::size_t>(k)]) * power[static_cast<std::size_t>(k)];
            out[static_cast<std::size_t>(b)].push_back(static_cast<float>(10.0 * std::log10(sum + 1.0e-20)));
        }
    }
    return out;
}

}  // namespace

SpectralDistance spectralDistance(const std::vector<float>& a, const std::vector<float>& b, double rate)
{
    SpectralDistance result;
    if (a.empty() && b.empty())
        return result;
    const std::size_t n = std::max(a.size(), b.size());
    auto pa = a, pb = b;
    pa.resize(n, 0.0f);
    pb.resize(n, 0.0f);

    const int frames[3] = {256, 1024, 4096}, bands[3] = {32, 64, 96};
    for (int r = 0; r < 3; ++r)
    {
        const auto sa = melSpectrogram(pa, frames[r], bands[r], rate);
        const auto sb = melSpectrogram(pb, frames[r], bands[r], rate);
        float loudest = -1.0e9f;
        for (const auto& row : sa)
            for (float v : row)
                loudest = std::max(loudest, v);
        const float floorDb = loudest - 80.0f;
        double sum = 0.0;
        long count = 0;
        for (std::size_t i = 0; i < sa.size(); ++i)
            for (std::size_t t = 0; t < sa[i].size(); ++t)
            {
                const float da = std::max(sa[i][t], floorDb), db = std::max(sb[i][t], floorDb);
                if (da > floorDb || db > floorDb)
                {
                    sum += std::abs(da - db);
                    ++count;
                }
            }
        result.perResolution.push_back(count ? sum / static_cast<double>(count) : 0.0);
    }
    for (double v : result.perResolution)
        result.meanDb += v / 3.0;
    return result;
}

}  // namespace valis

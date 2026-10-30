// src/shot/Loudness.cpp

#include "valis/Loudness.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace valis {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

/// BS.1770 K-weighting: a high shelf for the head, then a high-pass. The published coefficients are for
/// 48 kHz; these formulas give them at any rate (the same derivation as the ITU's analogue prototype).
std::array<Biquad, 2> kWeighting(double rate)
{
    std::array<Biquad, 2> f;
    {
        const double f0 = 1681.974450955533, gain = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(kPi * f0 / rate), vh = std::pow(10.0, gain / 20.0), vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        f[0].b0 = (vh + vb * k / q + k * k) / a0;
        f[0].b1 = 2.0 * (k * k - vh) / a0;
        f[0].b2 = (vh - vb * k / q + k * k) / a0;
        f[0].a1 = 2.0 * (k * k - 1.0) / a0;
        f[0].a2 = (1.0 - k / q + k * k) / a0;
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(kPi * f0 / rate);
        const double a0 = 1.0 + k / q + k * k;
        f[1].b0 = 1.0; f[1].b1 = -2.0; f[1].b2 = 1.0;
        f[1].a1 = 2.0 * (k * k - 1.0) / a0;
        f[1].a2 = (1.0 - k / q + k * k) / a0;
    }
    return f;
}

double lufs(double meanSquare) { return -0.691 + 10.0 * std::log10(std::max(meanSquare, 1.0e-12)); }

/// Mean-square of K-weighted sliding blocks, summed over channels: one value per hop.
std::vector<double> blockPowers(const std::vector<std::vector<double>>& weighted, double rate, double window, double hop)
{
    const std::size_t w = static_cast<std::size_t>(window * rate), h = static_cast<std::size_t>(hop * rate);
    const std::size_t n = weighted.front().size();
    std::vector<double> powers;
    if (n < w || h == 0)
        return powers;
    std::vector<std::vector<double>> cumulative;
    for (const auto& ch : weighted)
    {
        std::vector<double> c(n + 1, 0.0);
        for (std::size_t i = 0; i < n; ++i)
            c[i + 1] = c[i] + ch[i] * ch[i];
        cumulative.push_back(std::move(c));
    }
    for (std::size_t s = 0; s + w <= n; s += h)
    {
        double sum = 0.0;
        for (const auto& c : cumulative)
            sum += (c[s + w] - c[s]) / static_cast<double>(w);
        powers.push_back(sum);
    }
    return powers;
}

/// Mean of the powers above a gate, in LUFS, or -120 when none are.
double gatedLufs(const std::vector<double>& powers, double gateLufs)
{
    double sum = 0.0;
    int count = 0;
    for (double p : powers)
        if (lufs(p) > gateLufs) { sum += p; ++count; }
    return count ? lufs(sum / count) : -120.0;
}

double percentile(std::vector<double> v, double p)
{
    std::sort(v.begin(), v.end());
    const double pos = p * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos);
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
}

/// Peak after 4x interpolation with a Hann-windowed sinc of 16 taps per phase: close enough to the
/// BS.1770 Annex 2 filter to see an inter-sample peak to within a few tenths of a dB.
double truePeak(const std::vector<std::vector<float>>& channels)
{
    constexpr int taps = 16, factor = 4;
    std::array<std::array<double, taps>, factor> phase{};
    for (int p = 0; p < factor; ++p)
        for (int t = 0; t < taps; ++t)
        {
            const double x = (t - taps / 2 + 1) - static_cast<double>(p) / factor;
            const double sinc = std::abs(x) < 1.0e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double window = 0.5 + 0.5 * std::cos(kPi * x / (taps / 2 + 1));
            phase[static_cast<std::size_t>(p)][static_cast<std::size_t>(t)] = sinc * window;
        }
    double peak = 0.0;
    for (const auto& ch : channels)
    {
        const long n = static_cast<long>(ch.size());
        for (long i = 0; i < n; ++i)
            for (int p = 0; p < factor; ++p)
            {
                double acc = 0.0;
                for (int t = 0; t < taps; ++t)
                {
                    const long j = i + t - taps / 2 + 1;
                    if (j >= 0 && j < n)
                        acc += phase[static_cast<std::size_t>(p)][static_cast<std::size_t>(t)] * ch[static_cast<std::size_t>(j)];
                }
                peak = std::max(peak, std::abs(acc));
            }
    }
    return 20.0 * std::log10(std::max(peak, 1.0e-6));
}

}  // namespace

LoudnessReport measureLoudness(const std::vector<float>& left, const std::vector<float>& right, double rate)
{
    LoudnessReport report;
    if (left.empty() || rate <= 0.0)
        return report;

    std::vector<std::vector<float>> channels{left, right.empty() ? left : right};
    channels[1].resize(channels[0].size(), 0.0f);

    std::vector<std::vector<double>> weighted;
    for (const auto& ch : channels)
    {
        auto filter = kWeighting(rate);
        std::vector<double> y(ch.size());
        for (std::size_t i = 0; i < ch.size(); ++i)
            y[i] = filter[1].process(filter[0].process(ch[i]));
        weighted.push_back(std::move(y));
    }

    const auto momentary = blockPowers(weighted, rate, 0.4, 0.1);
    if (! momentary.empty())
    {
        const double absolute = gatedLufs(momentary, -70.0);
        report.integratedLufs = gatedLufs(momentary, absolute - 10.0);
        for (double p : momentary)
            report.momentaryMaxLufs = std::max(report.momentaryMaxLufs, lufs(p));
    }

    const auto shortTerm = blockPowers(weighted, rate, 3.0, 1.0);
    std::vector<double> levels;
    for (double p : shortTerm)
        report.shortTermMaxLufs = std::max(report.shortTermMaxLufs, lufs(p));
    if (! shortTerm.empty())
    {
        const double gate = gatedLufs(shortTerm, -70.0) - 20.0;
        for (double p : shortTerm)
            if (lufs(p) > gate)
                levels.push_back(lufs(p));
        if (levels.size() > 1)
            report.rangeLu = percentile(levels, 0.95) - percentile(levels, 0.10);
    }

    report.truePeakDbtp = truePeak(channels);
    return report;
}

}  // namespace valis

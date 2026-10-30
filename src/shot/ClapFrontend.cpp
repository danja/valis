// src/shot/ClapFrontend.cpp

#include "valis/ClapFrontend.h"
#include "valis/NeuralEars.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace valis {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kFft = 1024, kHop = 480, kBins = kFft / 2 + 1;

double hzToSlaney(double hz)
{
    const double logStep = 27.0 / std::log(6.4);
    return hz >= 1000.0 ? 15.0 + std::log(hz / 1000.0) * logStep : 3.0 * hz / 200.0;
}

double slaneyToHz(double mel)
{
    const double logStep = std::log(6.4) / 27.0;
    return mel >= 15.0 ? 1000.0 * std::exp(logStep * (mel - 15.0)) : 200.0 * mel / 3.0;
}

/// kBins x kClapMels weights, Slaney scale and Slaney area normalisation (Hugging Face `mel_filter_bank`).
std::vector<std::vector<double>> slaneyBank()
{
    const double lo = hzToSlaney(50.0), hi = hzToSlaney(14000.0);
    std::vector<double> edges(kClapMels + 2);
    for (int i = 0; i < kClapMels + 2; ++i)
        edges[static_cast<std::size_t>(i)] = slaneyToHz(lo + (hi - lo) * i / (kClapMels + 1));
    std::vector<std::vector<double>> bank(kClapMels, std::vector<double>(kBins, 0.0));
    for (int m = 0; m < kClapMels; ++m)
    {
        const double a = edges[static_cast<std::size_t>(m)], b = edges[static_cast<std::size_t>(m) + 1], c = edges[static_cast<std::size_t>(m) + 2];
        const double norm = 2.0 / (c - a);
        for (int k = 0; k < kBins; ++k)
        {
            const double f = static_cast<double>(k) * kClapRate / 2.0 / (kBins - 1);
            const double down = (f - a) / (b - a), up = (c - f) / (c - b);
            bank[static_cast<std::size_t>(m)][static_cast<std::size_t>(k)] = std::max(0.0, std::min(down, up)) * norm;
        }
    }
    return bank;
}

}  // namespace

std::vector<float> clapPrepare(const std::vector<float>& mono, bool zeroPad)
{
    std::vector<float> x = mono;
    if (x.size() < 480)
        x.resize(480, 0.0f);
    if (x.size() < static_cast<std::size_t>(kClapSamples) && ! zeroPad)
    {
        const std::size_t repeats = static_cast<std::size_t>(kClapSamples) / x.size();
        std::vector<float> tiled;
        tiled.reserve(x.size() * repeats);
        for (std::size_t r = 0; r < repeats; ++r)
            tiled.insert(tiled.end(), x.begin(), x.end());
        x = std::move(tiled);
    }
    x.resize(static_cast<std::size_t>(kClapSamples), 0.0f);
    return x;
}

std::vector<float> clapLogMel(const std::vector<float>& x)
{
    static const auto bank = slaneyBank();
    static const juce::dsp::FFT fft(10);
    std::vector<float> window(kFft);
    for (int i = 0; i < kFft; ++i)
        window[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / kFft));   // periodic Hann

    const long n = static_cast<long>(x.size());
    auto reflect = [&x, n](long i) {                       // numpy "reflect" padding, the edge sample not repeated
        if (i < 0) i = -i;
        if (i >= n) i = 2 * (n - 1) - i;
        return x[static_cast<std::size_t>(std::clamp<long>(i, 0, n - 1))];
    };

    std::vector<float> out(static_cast<std::size_t>(kClapFrames) * kClapMels);
    std::vector<float> buffer(2 * kFft);
    std::vector<double> power(kBins);
    for (int t = 0; t < kClapFrames; ++t)
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        for (int i = 0; i < kFft; ++i)
            buffer[static_cast<std::size_t>(i)] = reflect(static_cast<long>(t) * kHop + i - kFft / 2) * window[static_cast<std::size_t>(i)];
        fft.performRealOnlyForwardTransform(buffer.data(), true);
        for (int k = 0; k < kBins; ++k)
        {
            const double re = buffer[static_cast<std::size_t>(2 * k)], im = buffer[static_cast<std::size_t>(2 * k) + 1];
            power[static_cast<std::size_t>(k)] = re * re + im * im;
        }
        for (int m = 0; m < kClapMels; ++m)
        {
            double sum = 0.0;
            for (int k = 0; k < kBins; ++k)
                sum += bank[static_cast<std::size_t>(m)][static_cast<std::size_t>(k)] * power[static_cast<std::size_t>(k)];
            out[static_cast<std::size_t>(t) * kClapMels + static_cast<std::size_t>(m)] =
                static_cast<float>(10.0 * std::log10(std::max(sum, 1.0e-10)));
        }
    }
    return out;
}

float cosine(const std::vector<float>& a, const std::vector<float>& b)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        sum += static_cast<double>(a[i]) * b[i];
    return static_cast<float>(sum);
}

}  // namespace valis

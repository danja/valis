// src/shot/AstFrontend.cpp

#include "valis/AstFrontend.h"

#include <functional>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace valis {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kFrame = 400, kShift = 160, kFft = 512, kBins = kFft / 2;   // Kaldi leaves out the Nyquist bin
constexpr double kMean = -4.2677393, kStd = 4.5689974;                    // AST's AudioSet statistics

double hzToKaldi(double hz) { return 1127.0 * std::log(1.0 + hz / 700.0); }

/// Triangles that are straight lines in mel, not in Hz: Kaldi's rule, 20 Hz to the Nyquist frequency.
std::vector<std::vector<double>> kaldiBank()
{
    const double lo = hzToKaldi(20.0), hi = hzToKaldi(kAstRate / 2.0), delta = (hi - lo) / (kAstMels + 1);
    std::vector<std::vector<double>> bank(kAstMels, std::vector<double>(kBins, 0.0));
    for (int m = 0; m < kAstMels; ++m)
    {
        const double a = lo + delta * m, b = a + delta, c = b + delta;
        for (int k = 0; k < kBins; ++k)
        {
            const double mel = hzToKaldi(static_cast<double>(k) * kAstRate / kFft);
            const double down = (mel - a) / (b - a), up = (c - mel) / (c - b);
            bank[static_cast<std::size_t>(m)][static_cast<std::size_t>(k)] = std::max(0.0, std::min(down, up));
        }
    }
    return bank;
}

}  // namespace

std::vector<float> resampleLinear(const std::vector<float>& x, double fromRate, double toRate)
{
    if (x.empty() || std::equal_to<>{}(fromRate, toRate))
        return x;
    const std::size_t n = static_cast<std::size_t>(static_cast<double>(x.size()) * toRate / fromRate);
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double pos = static_cast<double>(i) * fromRate / toRate;
        const std::size_t j = std::min(static_cast<std::size_t>(pos), x.size() - 1), k = std::min(j + 1, x.size() - 1);
        const double frac = pos - static_cast<double>(j);
        out[i] = static_cast<float>(x[j] * (1.0 - frac) + x[k] * frac);
    }
    return out;
}

std::vector<float> astFilterbank(const std::vector<float>& x)
{
    static const auto bank = kaldiBank();
    static const juce::dsp::FFT fft(9);
    std::vector<double> window(kFrame);
    for (int i = 0; i < kFrame; ++i)
        window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (kFrame - 1));   // symmetric Hann

    std::vector<float> out(static_cast<std::size_t>(kAstFrames) * kAstMels, 0.0f);   // missing frames are zero
    const long frames = x.size() >= static_cast<std::size_t>(kFrame) ? 1 + (static_cast<long>(x.size()) - kFrame) / kShift : 0;
    std::vector<float> buffer(2 * kFft);
    std::vector<double> frame(kFrame), power(kBins);
    for (long t = 0; t < std::min<long>(frames, kAstFrames); ++t)
    {
        double mean = 0.0;
        for (int i = 0; i < kFrame; ++i)
            mean += x[static_cast<std::size_t>(t * kShift + i)];
        mean /= kFrame;
        for (int i = 0; i < kFrame; ++i)
            frame[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(t * kShift + i)] - mean;      // remove the DC offset
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        buffer[0] = static_cast<float>(frame[0] * (1.0 - 0.97) * window[0]);                               // 0.97 pre-emphasis
        for (int i = 1; i < kFrame; ++i)
            buffer[static_cast<std::size_t>(i)] = static_cast<float>((frame[static_cast<std::size_t>(i)] - 0.97 * frame[static_cast<std::size_t>(i) - 1]) * window[static_cast<std::size_t>(i)]);
        fft.performRealOnlyForwardTransform(buffer.data(), true);
        for (int k = 0; k < kBins; ++k)
        {
            const double re = buffer[static_cast<std::size_t>(2 * k)], im = buffer[static_cast<std::size_t>(2 * k) + 1];
            power[static_cast<std::size_t>(k)] = re * re + im * im;
        }
        for (int m = 0; m < kAstMels; ++m)
        {
            double sum = 0.0;
            for (int k = 0; k < kBins; ++k)
                sum += bank[static_cast<std::size_t>(m)][static_cast<std::size_t>(k)] * power[static_cast<std::size_t>(k)];
            const double logMel = std::log(std::max(sum, 1.1920928955078125e-07));
            out[static_cast<std::size_t>(t) * kAstMels + static_cast<std::size_t>(m)] = static_cast<float>((logMel - kMean) / (kStd * 2.0));
        }
    }
    // Frames past the end of a short sound are zeros in the *unnormalised* domain, as the extractor pads before it normalises.
    const long filled = std::min<long>(frames, kAstFrames);
    const float zeroValue = static_cast<float>((0.0 - kMean) / (kStd * 2.0));
    for (long t = filled; t < kAstFrames; ++t)
        std::fill_n(out.begin() + t * kAstMels, kAstMels, zeroValue);
    return out;
}

}  // namespace valis

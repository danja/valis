// src/ops/TempoEstimate.cpp

#include "valis/TempoEstimate.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace valis {

namespace {

constexpr int kOrder = 10;
constexpr int kFrame = 1 << kOrder;     // 1024 samples
constexpr int kHop = kFrame / 4;        // finer than the frame, so a beat is placed to a few milliseconds
constexpr double kLowestBpm = 40.0;
constexpr double kHighestBpm = 200.0;
constexpr double kMinSeconds = 8.0;
constexpr std::size_t kMaxCandidates = 4;
constexpr double kDistinct = 0.03;      // candidates closer than this fraction are the same tempo
// The flux of a steady sound is rounding error, which normalising would stretch into a pulse;
// a real onset moves it by tens or more.
constexpr double kMinFluxRms = 0.5;
constexpr double kMinStrength = 0.02;   // below this the curve does not repeat
// Listeners favour tempos near 120 BPM, so a pulse that also repeats at half or a third of its
// speed is ranked by its strength times a log-normal preference (Ellis, "Beat tracking by dynamic
// programming", J. New Music Research 36, 2007). The strength reported is not weighted.
constexpr double kPreferredBpm = 120.0;
constexpr double kPreferenceOctaves = 1.4;

}  // namespace

TempoEstimate estimateTempo(const std::vector<float>& mono, double sampleRate)
{
    TempoEstimate tempo;
    if (sampleRate <= 0.0 || static_cast<double>(mono.size()) < kMinSeconds * sampleRate)
        return tempo;

    juce::dsp::FFT fft(kOrder);
    juce::dsp::WindowingFunction<float> window(kFrame, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> buffer(static_cast<std::size_t>(kFrame) * 2);
    std::vector<float> previous(kFrame / 2, 0.0f);

    // Spectral flux: the sum over bins of the rise in (compressed) magnitude since the last frame.
    std::vector<double> flux;
    for (std::size_t at = 0; at + kFrame <= mono.size(); at += kHop)
    {
        std::copy_n(mono.begin() + static_cast<std::ptrdiff_t>(at), kFrame, buffer.begin());
        std::fill(buffer.begin() + kFrame, buffer.end(), 0.0f);
        window.multiplyWithWindowingTable(buffer.data(), kFrame);
        fft.performFrequencyOnlyForwardTransform(buffer.data());
        double rise = 0.0;
        for (std::size_t b = 0; b < previous.size(); ++b)
        {
            // Compressed, so a loud bin does not hide the rest.
            const float now = std::log1p(1000.0f * buffer[b] / (kFrame / 2));
            rise += std::max(0.0f, now - previous[b]);
            previous[b] = now;
        }
        flux.push_back(rise);
    }
    // The first frame rises from nothing, which is not a beat.
    if (! flux.empty())
        flux.front() = 0.0;

    const double mean = std::accumulate(flux.begin(), flux.end(), 0.0) / static_cast<double>(std::max<std::size_t>(flux.size(), 1));
    for (auto& f : flux) f -= mean;

    const double framesPerSecond = sampleRate / kHop;
    const auto lagFor = [&](double bpm) { return 60.0 / bpm * framesPerSecond; };
    const std::size_t lagMin = static_cast<std::size_t>(std::floor(lagFor(kHighestBpm))) - 1;
    const std::size_t lagMax = static_cast<std::size_t>(std::ceil(lagFor(kLowestBpm))) + 1;
    if (flux.size() < lagMax * 3)
        return tempo;

    // Autocorrelation at each delay, divided by the number of terms so a long delay is not
    // penalised for having fewer, then by the value at zero delay.
    double zero = 0.0;
    for (const double f : flux) zero += f * f;
    zero /= static_cast<double>(flux.size());
    if (std::sqrt(zero) < kMinFluxRms)
        return tempo;

    std::vector<double> ac(lagMax + 2, 0.0);
    for (std::size_t lag = lagMin - 1; lag <= lagMax + 1; ++lag)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i + lag < flux.size(); ++i)
            sum += flux[i] * flux[i + lag];
        ac[lag] = sum / static_cast<double>(flux.size() - lag) / zero;
    }

    struct Peak { double lag; double strength; double score; };
    std::vector<Peak> peaks;
    for (std::size_t lag = lagMin; lag <= lagMax; ++lag)
    {
        if (! (ac[lag] > ac[lag - 1] && ac[lag] >= ac[lag + 1]) || ac[lag] < kMinStrength)
            continue;
        // A parabola through the peak and its neighbours places it between whole delays.
        const double a = ac[lag - 1], b = ac[lag], c = ac[lag + 1];
        const double bend = a - 2.0 * b + c;
        const double offset = bend < 0.0 ? std::clamp(0.5 * (a - c) / bend, -0.5, 0.5) : 0.0;
        const double peakLag = static_cast<double>(lag) + offset;
        const double octaves = std::log2(60.0 * framesPerSecond / peakLag / kPreferredBpm);
        peaks.push_back({peakLag, b, b * std::exp(-0.5 * (octaves / kPreferenceOctaves) * (octaves / kPreferenceOctaves))});
    }
    std::sort(peaks.begin(), peaks.end(), [](const Peak& x, const Peak& y) { return x.score > y.score; });

    for (const auto& p : peaks)
    {
        const double bpm = 60.0 * framesPerSecond / p.lag;
        if (bpm < kLowestBpm || bpm > kHighestBpm)
            continue;
        const bool repeat = std::any_of(tempo.candidates.begin(), tempo.candidates.end(),
                                        [&](const TempoCandidate& c) { return std::abs(c.bpm - bpm) < kDistinct * c.bpm; });
        if (repeat)
            continue;
        tempo.candidates.push_back({bpm, std::min(p.strength, 1.0)});
        if (tempo.candidates.size() == kMaxCandidates)
            break;
    }
    tempo.found = ! tempo.candidates.empty();
    return tempo;
}

}  // namespace valis

// src/shot/ModalFit.cpp

#include "valis/ModalFit.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

namespace valis {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Frequencies of the strongest peaks in the spectrum of the start of the sound, strongest first.
std::vector<double> spectralPeaks(const std::vector<float>& x, double rate, int count)
{
    constexpr int kOrder = 16;
    constexpr std::size_t kSize = std::size_t{1} << kOrder;
    const std::size_t used = std::min<std::size_t>(x.size(), 32768);

    std::vector<float> data(kSize * 2, 0.0f);
    for (std::size_t n = 0; n < used; ++n)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * (static_cast<double>(n) + 0.5) / static_cast<double>(used));
        data[n] = static_cast<float>(x[n] * w);
    }

    juce::dsp::FFT fft(kOrder);
    fft.performFrequencyOnlyForwardTransform(data.data());

    const double binHz = rate / static_cast<double>(kSize);
    const auto lowBin  = static_cast<std::size_t>(30.0 / binHz);
    const auto highBin = std::min<std::size_t>(kSize / 2 - 2, static_cast<std::size_t>(std::min(20000.0, rate * 0.45) / binHz));

    float loudest = 0.0f;
    for (std::size_t b = lowBin; b <= highBin; ++b)
        loudest = std::max(loudest, data[b]);
    if (loudest <= 0.0f)
        return {};

    struct Peak { double hertz; float level; };
    std::vector<Peak> peaks;
    const float floorLevel = loudest * 0.001f;   // 60 dB under the strongest
    for (std::size_t b = std::max<std::size_t>(lowBin, 1); b <= highBin; ++b)
    {
        if (data[b] < floorLevel || data[b] < data[b - 1] || data[b] <= data[b + 1])
            continue;

        // A parabola through the three bins in dB puts the peak between them.
        const double a = std::log(std::max(data[b - 1], 1.0e-20f));
        const double m = std::log(std::max(data[b],     1.0e-20f));
        const double c = std::log(std::max(data[b + 1], 1.0e-20f));
        const double d = a - 2.0 * m + c;
        const double offset = d != 0.0 ? std::clamp(0.5 * (a - c) / d, -0.5, 0.5) : 0.0;
        peaks.push_back({(static_cast<double>(b) + offset) * binHz, data[b]});
    }

    std::sort(peaks.begin(), peaks.end(), [](const Peak& p, const Peak& q) { return p.level > q.level; });

    std::vector<double> chosen;
    for (const auto& p : peaks)
    {
        const bool crowded = std::any_of(chosen.begin(), chosen.end(),
                                         [&](double f) { return std::abs(f - p.hertz) < 10.0; });
        if (! crowded)
            chosen.push_back(p.hertz);
        if (static_cast<int>(chosen.size()) >= count)
            break;
    }
    return chosen;
}

/// The ring time and starting level of one frequency, from its amplitude over time.
bool measureMode(const std::vector<float>& x, double rate, double hertz, FittedMode& mode)
{
    constexpr std::size_t kWindow = 4096, kHop = 512;
    const double omega = 2.0 * kPi * hertz / rate;

    std::vector<double> window(kWindow);
    double windowSum = 0.0;
    for (std::size_t n = 0; n < kWindow; ++n)
    {
        window[n] = 0.5 - 0.5 * std::cos(2.0 * kPi * (static_cast<double>(n) + 0.5) / static_cast<double>(kWindow));
        windowSum += window[n];
    }

    std::vector<double> time, logAmp;
    double first = 0.0;
    for (std::size_t start = 0; start + kWindow <= x.size(); start += kHop)
    {
        std::complex<double> sum = 0.0;
        for (std::size_t n = 0; n < kWindow; ++n)
            sum += static_cast<double>(x[start + n]) * window[n]
                   * std::polar(1.0, -omega * static_cast<double>(start + n));

        const double amplitude = std::abs(sum) / (windowSum * 0.5);
        if (time.empty())
            first = amplitude;
        else if (amplitude < first * 0.01 || amplitude <= 0.0)
            break;   // forty decibels down: below here the neighbours and the noise dominate

        if (amplitude <= 0.0)
            break;
        time.push_back((static_cast<double>(start) + kWindow * 0.5) / rate);
        logAmp.push_back(std::log(amplitude));
    }
    if (time.empty())
        return false;

    // A least-squares line through ln(amplitude) against time.
    const auto count = static_cast<double>(time.size());
    double st = 0.0, sy = 0.0, stt = 0.0, sty = 0.0;
    for (std::size_t i = 0; i < time.size(); ++i)
    {
        st += time[i]; sy += logAmp[i]; stt += time[i] * time[i]; sty += time[i] * logAmp[i];
    }
    const double denominator = count * stt - st * st;
    double slope = 0.0, intercept = logAmp.front();
    if (time.size() >= 3 && denominator > 0.0)
    {
        slope     = (count * sty - st * sy) / denominator;
        intercept = (sy - slope * st) / count;
    }

    mode.hertz   = hertz;
    mode.seconds = slope < -0.1151 ? std::clamp(std::log(1000.0) / -slope, 0.02, 60.0) : 60.0;   // 0.1151/s is 60 s
    mode.gain    = std::min(100.0, std::exp(intercept));
    return true;
}

}  // namespace

std::vector<FittedMode> fitModes(const std::vector<float>& mono, double sampleRate, int maxModes)
{
    if (mono.size() < 4096 || maxModes < 1)
        return {};

    float peak = 0.0f;
    for (const float s : mono)
        peak = std::max(peak, std::abs(s));
    if (! (peak > 0.0f))
        return {};

    std::size_t onset = 0;
    while (onset < mono.size() && std::abs(mono[onset]) < peak * 0.1f)
        ++onset;

    const auto length = std::min<std::size_t>(mono.size() - onset, static_cast<std::size_t>(1.5 * sampleRate));
    if (length < 4096)
        return {};
    const std::vector<float> sound(mono.begin() + static_cast<std::ptrdiff_t>(onset),
                                   mono.begin() + static_cast<std::ptrdiff_t>(onset + length));

    // Take more peaks than modes asked for: some will not decay cleanly and are dropped.
    std::vector<FittedMode> found;
    for (const double hertz : spectralPeaks(sound, sampleRate, maxModes * 2))
    {
        FittedMode mode;
        if (measureMode(sound, sampleRate, hertz, mode) && mode.gain > 0.0)
            found.push_back(mode);
    }

    std::sort(found.begin(), found.end(), [](const FittedMode& a, const FittedMode& b) { return a.gain > b.gain; });
    if (static_cast<int>(found.size()) > maxModes)
        found.resize(static_cast<std::size_t>(maxModes));
    std::sort(found.begin(), found.end(), [](const FittedMode& a, const FittedMode& b) { return a.hertz < b.hertz; });
    return found;
}

std::string modesToString(const std::vector<FittedMode>& modes)
{
    std::string text;
    for (const auto& m : modes)
    {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "%.2f:%.3f:%.4g", m.hertz, m.seconds, m.gain);
        text += (text.empty() ? "" : " ") + std::string(buffer);
    }
    return text;
}

std::vector<float> renderModes(const std::vector<FittedMode>& modes, double sampleRate, std::size_t samples)
{
    std::vector<float> out(samples, 0.0f);
    for (const auto& m : modes)
    {
        const double omega = 2.0 * kPi * m.hertz / sampleRate;
        const double r = std::pow(0.001, 1.0 / (sampleRate * m.seconds));
        double decay = 1.0;
        for (std::size_t n = 0; n < samples; ++n)
        {
            out[n] += static_cast<float>(m.gain * decay * std::sin(static_cast<double>(n + 1) * omega));
            decay *= r;
        }
    }
    return out;
}

}  // namespace valis

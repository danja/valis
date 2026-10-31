// src/ops/SectionMap.cpp

#include "valis/SectionMap.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace valis {

namespace {

constexpr int kOrder = 13;   // 8192 samples: 5.9 Hz bins at 48 kHz, fine enough to tell bass notes apart
constexpr int kFrame = 1 << kOrder;
constexpr int kHop = kFrame / 2;
// Pitch class is read only where a bin is narrower than a semitone, so every class has bins
// to fall in, and below the range where the content is mostly noise. A bass note is heard
// through its harmonics.
constexpr double kChromaLowestHz = 130.0;
constexpr double kChromaHighestHz = 5000.0;
constexpr int kMelBands = 64;
constexpr int kDims = kMelBands + 12;
constexpr std::size_t kWindow = 5;       // seconds either side of a candidate boundary
constexpr std::size_t kMinGap = 10;      // seconds between boundaries
constexpr std::size_t kMinSeconds = 20;
constexpr double kMelSpreadFloor = 0.1;     // log-level units
constexpr double kChromaSpreadFloor = 0.02; // share of pitched energy
constexpr double kMinNorms = 1.0;           // below this product of the two average vectors' lengths there is no change to measure

double mel(double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); }

/// The triangular weight of mel band `band` at a frequency, with 66 evenly spaced mel points
/// from 20 Hz to Nyquist.
double melWeight(int band, double melValue, double melLow, double melStep)
{
    const double left = melLow + melStep * band, centre = left + melStep, right = centre + melStep;
    if (melValue <= left || melValue >= right)
        return 0.0;
    return melValue < centre ? (melValue - left) / (centre - left) : (right - melValue) / (right - centre);
}

}  // namespace

KeyEstimate estimateKey(const std::array<double, 12>& chroma)
{
    // Krumhansl and Kessler's probe-tone ratings for a major and a minor key on C.
    static constexpr std::array<double, 12> major = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static constexpr std::array<double, 12> minor = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

    auto mean = [](const std::array<double, 12>& v) { double t = 0.0; for (const double x : v) t += x; return t / 12.0; };
    const double meanChroma = mean(chroma);
    double chromaSpread = 0.0;
    for (const double x : chroma) chromaSpread += (x - meanChroma) * (x - meanChroma);

    KeyEstimate best;
    best.correlation = -2.0;
    if (chromaSpread <= 1.0e-12)
    {
        best.correlation = 0.0;
        return best;
    }
    for (int root = 0; root < 12; ++root)
        for (const bool isMinor : {false, true})
        {
            const auto& profile = isMinor ? minor : major;
            const double meanProfile = mean(profile);
            double cross = 0.0, profileSpread = 0.0;
            for (int pc = 0; pc < 12; ++pc)
            {
                const double p = profile[static_cast<std::size_t>(((pc - root) + 12) % 12)] - meanProfile;
                cross += p * (chroma[static_cast<std::size_t>(pc)] - meanChroma);
                profileSpread += p * p;
            }
            const double r = cross / std::sqrt(profileSpread * chromaSpread);
            if (r > best.correlation)
                best = {root, isMinor, r};
        }
    return best;
}

std::string keyName(const KeyEstimate& key)
{
    if (! (key.correlation > 0.0))
        return "none";
    return std::string(pitchClassName(key.root)) + (key.minor ? " minor" : " major");
}

const char* pitchClassName(int pitchClass)
{
    static const std::array<const char*, 12> names = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[static_cast<std::size_t>(((pitchClass % 12) + 12) % 12)];
}

SectionMap findSections(const std::vector<float>& mono, double sampleRate, const StereoImage* stereo)
{
    SectionMap map;
    if (sampleRate <= 0.0)
        return map;
    const std::size_t perSecond = static_cast<std::size_t>(sampleRate);
    const std::size_t seconds = perSecond > 0 ? mono.size() / perSecond : 0;
    if (seconds < kMinSeconds || perSecond < static_cast<std::size_t>(kFrame))
        return map;

    // Which mel band weights and pitch class each FFT bin feeds.
    const double binHz = sampleRate / kFrame;
    const double melLow = mel(20.0), melStep = (mel(sampleRate / 2.0) - melLow) / (kMelBands + 1);
    struct BinInfo { std::array<std::pair<int, float>, 2> bands{}; int used = 0; int pitchClass = -1; };
    std::vector<BinInfo> bins(kFrame / 2);
    for (int b = 1; b < kFrame / 2; ++b)
    {
        const double hz = b * binHz;
        auto& info = bins[static_cast<std::size_t>(b)];
        const double m = mel(hz);
        // A bin lies under at most two triangles.
        const int first = std::clamp(static_cast<int>(std::floor((m - melLow) / melStep)) - 1, 0, kMelBands - 1);
        for (int band = first; band < std::min(first + 2, kMelBands); ++band)
        {
            const double w = melWeight(band, m, melLow, melStep);
            if (w > 0.0 && info.used < 2)
                info.bands[static_cast<std::size_t>(info.used++)] = {band, static_cast<float>(w)};
        }
        if (hz >= kChromaLowestHz && hz <= kChromaHighestHz)
        {
            const long note = std::lround(69.0 + 12.0 * std::log2(hz / 440.0));
            info.pitchClass = static_cast<int>(((note % 12) + 12) % 12);
        }
    }

    juce::dsp::FFT fft(kOrder);
    juce::dsp::WindowingFunction<float> window(kFrame, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> buffer(static_cast<std::size_t>(kFrame) * 2);

    // One feature row per second, and the second's RMS level.
    std::vector<std::array<double, kDims>> rows(seconds);
    std::vector<double> rmsDb(seconds, -120.0);
    std::vector<std::array<double, 12>> rawChroma(seconds);   // before standardising, for the dominant pitch class
    for (std::size_t s = 0; s < seconds; ++s)
    {
        const std::size_t begin = s * perSecond;
        std::array<double, kMelBands> melSum{};
        std::array<double, 12> chromaSum{};
        double squares = 0.0;
        for (std::size_t i = 0; i < perSecond; ++i)
            squares += static_cast<double>(mono[begin + i]) * mono[begin + i];
        rmsDb[s] = 20.0 * std::log10(std::max(std::sqrt(squares / static_cast<double>(perSecond)), 1.0e-6));

        int frames = 0;
        for (std::size_t at = begin; at + kFrame <= begin + perSecond; at += kHop, ++frames)
        {
            std::copy_n(mono.begin() + static_cast<std::ptrdiff_t>(at), kFrame, buffer.begin());
            std::fill(buffer.begin() + kFrame, buffer.end(), 0.0f);
            window.multiplyWithWindowingTable(buffer.data(), kFrame);
            fft.performFrequencyOnlyForwardTransform(buffer.data());
            std::array<double, 12> chromaFrame{};
            double total = 0.0;
            for (int b = 1; b < kFrame / 2; ++b)
            {
                const double magnitude = buffer[static_cast<std::size_t>(b)] / (kFrame / 2);
                const auto& info = bins[static_cast<std::size_t>(b)];
                for (int k = 0; k < info.used; ++k)
                    melSum[static_cast<std::size_t>(info.bands[static_cast<std::size_t>(k)].first)]
                        += magnitude * info.bands[static_cast<std::size_t>(k)].second;
                if (info.pitchClass >= 0)
                {
                    chromaFrame[static_cast<std::size_t>(info.pitchClass)] += magnitude;
                    total += magnitude;
                }
            }
            // Chroma is a share of each frame's pitched energy, so a loud frame counts no more than a quiet one.
            for (std::size_t p = 0; p < 12; ++p)
                chromaSum[p] += chromaFrame[p] / (total + 1.0e-9);
        }

        const double n = std::max(frames, 1);
        auto& row = rows[s];
        // Levels are compressed with log(1 + 1000 x) so quiet bands still count.
        for (std::size_t band = 0; band < kMelBands; ++band)
            row[band] = std::log1p(1000.0 * melSum[band] / n);
        for (std::size_t p = 0; p < 12; ++p)
            row[kMelBands + p] = rawChroma[s][p] = chromaSum[p] / n;
    }

    // Standardise each dimension over the track.
    for (std::size_t d = 0; d < kDims; ++d)
    {
        double mean = 0.0, var = 0.0;
        for (const auto& r : rows) mean += r[d];
        mean /= static_cast<double>(seconds);
        for (const auto& r : rows) var += (r[d] - mean) * (r[d] - mean);
        const double sd = std::sqrt(var / static_cast<double>(seconds));
        // A floor on the divisor, so a dimension that barely moves is not stretched until its noise looks like change.
        const double floor = d < kMelBands ? kMelSpreadFloor : kChromaSpreadFloor;
        for (auto& r : rows) r[d] = (r[d] - mean) / std::max(sd, floor);
    }

    // Novelty: how unlike the five seconds after are from the five before.
    map.novelty.assign(seconds, 0.0);
    for (std::size_t t = kWindow; t + kWindow < seconds; ++t)
    {
        std::array<double, kDims> a{}, b{};
        for (std::size_t k = 0; k < kWindow; ++k)
            for (std::size_t d = 0; d < kDims; ++d)
            {
                a[d] += rows[t - kWindow + k][d] / kWindow;
                b[d] += rows[t + k][d] / kWindow;
            }
        double dot = 0.0, na = 0.0, nb = 0.0;
        for (std::size_t d = 0; d < kDims; ++d)
        {
            dot += a[d] * b[d];
            na += a[d] * a[d];
            nb += b[d] * b[d];
        }
        const double norms = std::sqrt(na) * std::sqrt(nb);
        map.novelty[t] = norms < kMinNorms ? 0.0 : 1.0 - dot / norms;
    }

    double mean = 0.0, var = 0.0;
    for (const double v : map.novelty) mean += v;
    mean /= static_cast<double>(seconds);
    for (const double v : map.novelty) var += (v - mean) * (v - mean);
    const double threshold = mean + 0.5 * std::sqrt(var / static_cast<double>(seconds));

    std::vector<std::size_t> bounds{0};
    long last = -static_cast<long>(kMinGap) - 2;   // so the first boundary may fall as early as kWindow
    for (std::size_t t = kWindow; t + kWindow < seconds; ++t)
    {
        const std::size_t from = t >= 3 ? t - 3 : 0, to = std::min(t + 4, seconds);
        const double peak = *std::max_element(map.novelty.begin() + static_cast<std::ptrdiff_t>(from),
                                              map.novelty.begin() + static_cast<std::ptrdiff_t>(to));
        if (map.novelty[t] > threshold && map.novelty[t] >= peak && static_cast<long>(t) - last >= static_cast<long>(kMinGap))
        {
            bounds.push_back(t);
            last = static_cast<long>(t);
        }
    }
    bounds.push_back(seconds);

    std::array<double, 12> trackChroma{};
    for (std::size_t s = 0; s < seconds; ++s)
        for (std::size_t p = 0; p < 12; ++p)
            trackChroma[p] += rawChroma[s][p];
    map.key = estimateKey(trackChroma);

    for (std::size_t i = 0; i + 1 < bounds.size(); ++i)
    {
        const std::size_t a = bounds[i], b = bounds[i + 1];
        Section sec;
        sec.startSeconds = static_cast<double>(a);
        sec.endSeconds = static_cast<double>(b);
        double energy = 0.0;
        std::array<double, 12> chroma{};
        for (std::size_t s = a; s < b; ++s)
        {
            energy += rmsDb[s];
            for (std::size_t p = 0; p < 12; ++p)
                chroma[p] += rawChroma[s][p];
        }
        sec.energyDb = energy / static_cast<double>(b - a);
        sec.dominantPitchClass = static_cast<int>(std::max_element(chroma.begin(), chroma.end()) - chroma.begin());
        sec.key = estimateKey(chroma);

        if (stereo != nullptr && stereo->seconds.size() >= b)
        {
            double w = 0.0, c = 0.0;
            long used = 0;
            for (std::size_t s = a; s < b; ++s)
                if (stereo->seconds[s].valid)
                {
                    w += stereo->seconds[s].widthDb;
                    c += stereo->seconds[s].correlation;
                    ++used;
                }
            if (used > 0)
            {
                sec.hasStereo = true;
                sec.widthDb = w / static_cast<double>(used);
                sec.correlation = c / static_cast<double>(used);
            }
        }
        map.sections.push_back(sec);
    }
    return map;
}

}  // namespace valis

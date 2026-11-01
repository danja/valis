// src/ops/SongMix.cpp
#include "valis/SongMix.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

namespace valis {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Seconds kept after the last bar, before the fade, whatever the song's tail was.
constexpr double kMixTailSeconds = 4.0;

/// xorshift64*, so the room is the same on every platform and standard library.
struct Noise
{
    std::uint64_t state;
    explicit Noise(std::uint64_t seed) : state(seed * 0x9E3779B97F4A7C15ull + 1) {}

    double uniform()
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return static_cast<double>((state * 0x2545F4914F6CDD1Dull) >> 11) / 9007199254740992.0;   // [0, 1)
    }

    double gaussian()
    {
        const double u = std::max(uniform(), 1.0e-12);
        return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * kPi * uniform());
    }
};

double toDb(double x) { return 20.0 * std::log10(x + 1.0e-12); }

/// A synthetic room: exponentially decaying noise, different in each ear, rolled off above 4.5 kHz, 12 ms late.
void roomImpulse(double rate, std::vector<float>& left, std::vector<float>& right)
{
    Noise noise(7);
    const auto n = static_cast<std::size_t>(1.6 * rate);
    const auto delay = static_cast<std::size_t>(0.012 * rate);

    // one-pole low-pass by the bilinear transform
    const double k = std::tan(kPi * 4500.0 / rate);
    const double b = k / (1.0 + k);
    const double a = (k - 1.0) / (k + 1.0);

    for (auto* ir : {&left, &right})
    {
        std::vector<double> h(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(i) / rate;
            h[i] = noise.gaussian() * std::exp(-t / 0.4) * (1.0 - std::exp(-t / 0.004));
        }
        double x1 = 0.0, y1 = 0.0, energy = 0.0;
        for (auto& v : h)
        {
            const double y = b * v + b * x1 - a * y1;
            x1 = v;
            y1 = y;
            v = y;
            energy += y * y;
        }
        const double scale = 1.0 / std::sqrt(std::max(energy, 1.0e-30));
        ir->assign(delay + n, 0.0f);
        for (std::size_t i = 0; i < n; ++i)
            (*ir)[delay + i] = static_cast<float>(h[i] * scale);
    }
}

} // namespace

namespace songmix {

double playingRmsDb(const std::vector<float>& x, double sampleRate)
{
    const auto w = static_cast<std::size_t>(sampleRate / 10.0);
    if (w == 0 || x.size() < w)
        return -120.0;

    double sum = 0.0;
    std::size_t counted = 0;
    for (std::size_t at = 0; at + w <= x.size(); at += w)
    {
        double e = 0.0;
        for (std::size_t i = at; i < at + w; ++i)
            e += static_cast<double>(x[i]) * x[i];
        e /= static_cast<double>(w);
        if (std::sqrt(e) > 1.0e-3)
        {
            sum += e;
            ++counted;
        }
    }
    return counted == 0 ? -120.0 : toDb(std::sqrt(sum / static_cast<double>(counted)));
}

std::vector<float> fftConvolve(const std::vector<float>& signal, const std::vector<float>& impulse)
{
    std::vector<float> out(signal.size(), 0.0f);
    if (signal.empty() || impulse.empty())
        return out;

    // Overlap-add: blocks of the signal as long as the impulse (rounded up), transformed at twice that size.
    int order = 10;
    while ((std::size_t{1} << order) < 2 * impulse.size() && order < 22)
        ++order;
    const std::size_t size = std::size_t{1} << order;
    const std::size_t block = size - impulse.size() + 1;
    juce::dsp::FFT fft(order);

    std::vector<std::complex<float>> h(size), x(size), y(size);
    for (std::size_t i = 0; i < impulse.size(); ++i)
        h[i] = impulse[i];
    fft.perform(h.data(), x.data(), false);
    h = x;

    for (std::size_t at = 0; at < signal.size(); at += block)
    {
        const std::size_t n = std::min(block, signal.size() - at);
        std::fill(x.begin(), x.end(), std::complex<float>());
        for (std::size_t i = 0; i < n; ++i)
            x[i] = signal[at + i];
        fft.perform(x.data(), y.data(), false);
        for (std::size_t i = 0; i < size; ++i)
            y[i] *= h[i];
        fft.perform(y.data(), x.data(), true);
        const std::size_t span = std::min(size, signal.size() - at);
        for (std::size_t i = 0; i < span; ++i)
            out[at + i] += x[i].real();
    }
    return out;
}

void highpass(std::vector<float>& x, double cornerHz, double sampleRate)
{
    if (cornerHz <= 0.0)
        return;

    // The bilinear-transform Butterworth of order two, which is the RBJ high-pass at Q = 1/sqrt(2).
    const double w0 = 2.0 * kPi * std::min(cornerHz, 0.45 * sampleRate) / sampleRate;
    const double alpha = std::sin(w0) / (2.0 * 0.70710678118654752);
    const double c = std::cos(w0);
    const double a0 = 1.0 + alpha;
    const double b0 = (1.0 + c) / 2.0 / a0, b1 = -(1.0 + c) / a0, b2 = b0;
    const double a1 = -2.0 * c / a0, a2 = (1.0 - alpha) / a0;

    double z1 = 0.0, z2 = 0.0;      // transposed direct form II
    for (auto& v : x)
    {
        const double in = v;
        const double out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        v = static_cast<float>(out);
    }
}

std::vector<float> arcCurve(const Song& song, double sampleRate, std::size_t length)
{
    std::vector<double> steps(length, 0.0);
    std::size_t at = 0;
    for (const auto& s : song.sections)
    {
        const auto end = at + static_cast<std::size_t>(s.bars * song.barSeconds() * sampleRate);
        for (std::size_t i = at; i < std::min(end, length); ++i)
            steps[i] = s.level;
        at = end;
    }
    if (! song.sections.empty())
        for (std::size_t i = std::min(at, length); i < length; ++i)
            steps[i] = song.sections.back().level;

    // a moving average over about two seconds, held at the edges, by running sums
    const auto w = (static_cast<std::size_t>(2.0 * sampleRate)) | 1u;
    const std::size_t half = w / 2;
    std::vector<double> running(length + 2 * half + 1, 0.0);
    for (std::size_t i = 0; i < length + 2 * half; ++i)
    {
        const double v = length == 0 ? 0.0 : steps[i < half ? 0 : std::min(i - half, length - 1)];
        running[i + 1] = running[i] + v;
    }
    std::vector<float> curve(length);
    for (std::size_t i = 0; i < length; ++i)
        curve[i] = static_cast<float>(std::pow(10.0, (running[i + w] - running[i]) / static_cast<double>(w) / 20.0));
    return curve;
}

} // namespace songmix

bool mixStems(const Song& song, std::vector<MixStem>& stems, const MixOptions& options, MixReport& report,
              std::string& error)
{
    if (stems.empty())
    {
        error = "there are no stems to mix";
        return false;
    }
    const double rate = stems.front().audio.sampleRate;
    for (const auto& s : stems)
        if (std::abs(s.audio.sampleRate - rate) > 0.5 || s.audio.left.size() != s.audio.right.size())
        {
            error = "stem " + s.name + " is at " + std::to_string(s.audio.sampleRate) + " Hz, not " + std::to_string(rate);
            return false;
        }
    if (rate < 8000.0 || song.seconds() <= 0.0)
    {
        error = "the song has no length or the stems have no sample rate";
        return false;
    }

    const auto kept = static_cast<std::size_t>((song.seconds() + kMixTailSeconds) * rate);
    const auto rendered = static_cast<std::size_t>(song.renderSeconds() * rate);
    std::vector<float> left(std::max(rendered, kept), 0.0f), right(left.size(), 0.0f);

    report = MixReport{};
    for (auto& stem : stems)
    {
        songmix::highpass(stem.audio.left, stem.highpassHz, rate);
        songmix::highpass(stem.audio.right, stem.highpassHz, rate);

        std::vector<float> mono(stem.audio.frames());
        for (std::size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (stem.audio.left[i] + stem.audio.right[i]);
        const double measured = songmix::playingRmsDb(mono, rate);
        const double gainDb = stem.levelDb - measured;
        report.stems.push_back({stem.name, measured, gainDb});

        // constant power, with a stereo stem keeping its image and taking the pan as a balance
        const double g = std::pow(10.0, gainDb / 20.0) * std::sqrt(2.0);
        const double theta = (std::clamp(stem.pan, -1.0, 1.0) + 1.0) * kPi / 4.0;
        const auto gl = static_cast<float>(g * std::cos(theta)), gr = static_cast<float>(g * std::sin(theta));
        for (std::size_t i = 0; i < mono.size() && i < left.size(); ++i)
        {
            left[i] += stem.audio.left[i] * gl;
            right[i] += stem.audio.right[i] * gr;
        }
    }
    left.resize(kept);
    right.resize(kept);

    if (options.arc)
    {
        const auto curve = songmix::arcCurve(song, rate, kept);
        for (std::size_t i = 0; i < kept; ++i)
        {
            left[i] *= curve[i];
            right[i] *= curve[i];
        }
    }

    const auto fade = std::min(kept, static_cast<std::size_t>(song.fadeSeconds * rate));
    for (std::size_t i = 0; i < fade; ++i)
    {
        const float ramp = fade > 1 ? 1.0f - static_cast<float>(i) / static_cast<float>(fade - 1) : 0.0f;
        left[kept - fade + i] *= ramp;
        right[kept - fade + i] *= ramp;
    }

    if (song.roomAmount > 0.0)
    {
        std::vector<float> irLeft, irRight;
        roomImpulse(rate, irLeft, irRight);
        const auto wetLeft = songmix::fftConvolve(left, irLeft);
        const auto wetRight = songmix::fftConvolve(right, irRight);
        const auto amount = static_cast<float>(song.roomAmount);
        for (std::size_t i = 0; i < kept; ++i)
        {
            left[i] += wetLeft[i] * amount;
            right[i] += wetRight[i] * amount;
        }
    }

    // soft clip, then the peak at -1 dBFS
    const double drive = song.drive;
    float peak = 0.0f;
    for (std::size_t i = 0; i < kept; ++i)
    {
        left[i] = static_cast<float>(std::tanh(left[i] * drive) / drive);
        right[i] = static_cast<float>(std::tanh(right[i] * drive) / drive);
        peak = std::max({peak, std::abs(left[i]), std::abs(right[i])});
    }
    const float normalise = peak > 0.0f ? static_cast<float>(std::pow(10.0, -1.0 / 20.0)) / peak : 1.0f;
    for (std::size_t i = 0; i < kept; ++i)
    {
        left[i] *= normalise;
        right[i] *= normalise;
    }

    report.targetLufs = options.loudness ? options.loudness : song.loudness;
    auto measured = measureLoudness(left, right, rate);
    if (report.targetLufs)
    {
        const double wanted = *report.targetLufs - measured.integratedLufs;
        const double allowed = -1.0 - measured.truePeakDbtp;
        report.limitedByPeak = wanted > allowed;
        report.loudnessGainDb = report.limitedByPeak ? allowed : wanted;
        const auto gain = static_cast<float>(std::pow(10.0, report.loudnessGainDb / 20.0));
        for (std::size_t i = 0; i < kept; ++i)
        {
            left[i] *= gain;
            right[i] *= gain;
        }
        measured = measureLoudness(left, right, rate);
    }
    report.loudness = measured;

    // each section's playing level, so an arc can be checked by number
    std::size_t at = 0;
    for (const auto& s : song.sections)
    {
        const auto end = std::min(kept, at + static_cast<std::size_t>(s.bars * song.barSeconds() * rate));
        std::vector<float> mono;
        for (std::size_t i = std::min(at, kept); i < end; ++i)
            mono.push_back(0.5f * (left[i] + right[i]));
        report.sections.emplace_back(s.name, songmix::playingRmsDb(mono, rate));
        at += static_cast<std::size_t>(s.bars * song.barSeconds() * rate);
    }

    report.mix.sampleRate = rate;
    report.mix.left = std::move(left);
    report.mix.right = std::move(right);
    return true;
}

MixFilesResult mixSongFiles(const Song& song, const std::string& wavDirectory, const std::string& outputPath,
                            const MixOptions& options)
{
    MixFilesResult result;
    std::vector<MixStem> stems;
    for (const auto& s : song.stems)
    {
        const auto path = wavDirectory + "/" + s.name + ".wav";
        MixStem stem;
        std::string error;
        if (! readAudioFile(path, stem.audio, error))
        {
            result.missing.push_back(s.name);
            continue;
        }
        stem.name = s.name;
        stem.levelDb = s.level;
        stem.pan = s.pan;
        stem.highpassHz = s.highpass;
        stems.push_back(std::move(stem));
    }
    if (stems.empty())
    {
        result.error = "no stem wavs in " + wavDirectory + " (render the song first)";
        return result;
    }

    if (! mixStems(song, stems, options, result.report, result.error))
        return result;

    if (! writeWavFile(outputPath, result.report.mix, result.error))
        return result;

    result.outputPath = outputPath;
    result.ok = true;
    return result;
}

} // namespace valis

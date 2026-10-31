// src/ops/Analysis.cpp

#include "valis/Analysis.h"

#include <functional>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <sstream>

namespace valis {

namespace {

constexpr double kTwoPi = 6.283185307179586;

double toDb(double amplitude) { return 20.0 * std::log10(std::max(amplitude, 1.0e-6)); }

std::string number(double v)
{
    if (! std::isfinite(v))
        return "null";
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.6g", v);
    return buffer;
}

std::string quote(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out + "\"";
}

template <typename T>
std::string array(const std::vector<T>& values)
{
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i)
        out += (i ? "," : "") + number(static_cast<double>(values[i]));
    return out + "]";
}

/// The mono mix, with anything that is not a number counted and replaced by silence so one
/// bad sample cannot spoil every measurement after it.
std::vector<float> mixDown(const std::vector<float>& left, const std::vector<float>& right, long& nonFinite)
{
    const std::size_t n = std::max(left.size(), right.size());
    std::vector<float> mono(n, 0.0f);
    const int channels = right.empty() ? 1 : 2;
    for (std::size_t i = 0; i < n; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c)
        {
            const auto& source = c == 0 ? left : right;
            if (i >= source.size())
                continue;
            if (! std::isfinite(source[i]))
                ++nonFinite;
            else
                sum += source[i];
        }
        mono[i] = sum / static_cast<float>(channels);
    }
    return mono;
}

/// YIN on one frame. Returns the period in samples and the aperiodicity (0 perfectly
/// periodic), or period 0 if there is no dip below the threshold.
struct Frame { double period = 0.0; double aperiodicity = 1.0; };

Frame yinFrame(const float* x, int window, int minLag, int maxLag, std::vector<double>& difference)
{
    difference.assign(static_cast<std::size_t>(maxLag) + 1, 0.0);
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double sum = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const double d = static_cast<double>(x[i]) - x[i + tau];
            sum += d * d;
        }
        difference[static_cast<std::size_t>(tau)] = sum;
    }

    // Cumulative mean normalised difference: the step that stops the trivial dip at lag 0
    // and the octave errors a plain autocorrelation makes.
    std::vector<double> normalised(difference.size(), 1.0);
    double running = 0.0;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        running += difference[static_cast<std::size_t>(tau)];
        normalised[static_cast<std::size_t>(tau)] =
            running > 0.0 ? difference[static_cast<std::size_t>(tau)] * tau / running : 1.0;
    }

    // A strong second harmonic makes the half-period dip fall under the threshold too, so the first dip is taken
    // only if it is also near the deepest one (the whole-period dip is far deeper).
    constexpr double kThreshold = 0.15, kNearDeepest = 0.05;
    double deepest = 1.0;
    for (int tau = std::max(2, minLag); tau < maxLag; ++tau)
        deepest = std::min(deepest, normalised[static_cast<std::size_t>(tau)]);
    const double accept = std::min(kThreshold, deepest + kNearDeepest);

    int best = 0;
    for (int tau = std::max(2, minLag); tau < maxLag; ++tau)
    {
        if (normalised[static_cast<std::size_t>(tau)] < accept)
        {
            // Walk to the bottom of this dip.
            while (tau + 1 < maxLag && normalised[static_cast<std::size_t>(tau + 1)] < normalised[static_cast<std::size_t>(tau)])
                ++tau;
            best = tau;
            break;
        }
    }
    if (best == 0)
        return {};

    // Parabola through the dip for a period between whole samples.
    const double a = normalised[static_cast<std::size_t>(best - 1)];
    const double b = normalised[static_cast<std::size_t>(best)];
    const double c = normalised[static_cast<std::size_t>(best + 1)];
    const double denominator = a - 2.0 * b + c;
    const double offset = !std::equal_to<>{}(denominator, 0.0) ? 0.5 * (a - c) / denominator : 0.0;
    return { best + std::clamp(offset, -1.0, 1.0), b };
}

PitchEstimate estimatePitch(const std::vector<float>& mono, double sampleRate)
{
    PitchEstimate result;

    // About 24 kHz is plenty for pitches up to 4 kHz and keeps the search cheap.
    const int factor = std::max(1, static_cast<int>(std::lround(sampleRate / 24000.0)));
    const double rate = sampleRate / factor;
    std::vector<float> x(mono.size() / static_cast<std::size_t>(factor));
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        float sum = 0.0f;
        for (int k = 0; k < factor; ++k)
            sum += mono[i * static_cast<std::size_t>(factor) + static_cast<std::size_t>(k)];
        x[i] = sum / static_cast<float>(factor);
    }

    const int minLag = static_cast<int>(rate / 4000.0);   // a pitch above this cannot be found, and its half would be reported
    const int maxLag = static_cast<int>(rate / 50.0);
    const int window = static_cast<int>(rate * 0.04);   // 40 ms: two periods of 50 Hz
    const int hop = static_cast<int>(rate * 0.04);
    if (static_cast<int>(x.size()) < window + maxLag + 1)
        return result;

    std::vector<double> difference;
    std::vector<double> pitches;
    int frames = 0, voiced = 0;

    const int framesAvailable = (static_cast<int>(x.size()) - window - maxLag) / hop;
    const int step = std::max(1, framesAvailable / 120);   // at most about 120 frames analysed

    for (int f = 0; f <= framesAvailable; f += step)
    {
        const float* frame = x.data() + static_cast<std::size_t>(f) * static_cast<std::size_t>(hop);

        double energy = 0.0;
        for (int i = 0; i < window; ++i)
            energy += static_cast<double>(frame[i]) * frame[i];
        if (toDb(std::sqrt(energy / window)) < -55.0)
            continue;   // too quiet to have a pitch worth naming

        ++frames;
        const auto yin = yinFrame(frame, window, minLag, maxLag, difference);
        if (yin.period > 0.0 && yin.aperiodicity < 0.3)
        {
            ++voiced;
            pitches.push_back(rate / yin.period);
        }
    }

    if (frames == 0 || pitches.empty())
        return result;

    std::sort(pitches.begin(), pitches.end());
    const double median = pitches[pitches.size() / 2];

    int steady = 0;
    for (const double p : pitches)
        if (std::abs(1200.0 * std::log2(p / median)) < 50.0)
            ++steady;

    const double midi = 69.0 + 12.0 * std::log2(median / 440.0);
    result.found = voiced * 3 >= frames;   // a third of the sounding frames or more
    result.hz = median;
    result.midiNote = std::clamp(static_cast<int>(std::lround(midi)), 0, 127);
    result.cents = 100.0 * (midi - std::lround(midi));
    result.voicedFraction = static_cast<double>(voiced) / frames;
    result.stability = static_cast<double>(steady) / static_cast<double>(pitches.size());
    return result;
}

}  // namespace

std::string noteName(int midiNote)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int n = std::clamp(midiNote, 0, 127);
    return std::string(names[n % 12]) + std::to_string(n / 12 - 1);
}

SignalReport analyseSignal(const std::vector<float>& left, const std::vector<float>& right, double sampleRate)
{
    SignalReport report;
    report.sampleRate = sampleRate;
    report.channels = right.empty() ? 1 : 2;

    const std::size_t total = std::max(left.size(), right.size());
    report.seconds = sampleRate > 0.0 ? static_cast<double>(total) / sampleRate : 0.0;

    const auto mono = mixDown(left, right, report.nonFinite);

    // Level, DC and clipping, over every sample of every channel.
    double sumSquares = 0.0, sumMono = 0.0;
    long counted = 0;
    for (int c = 0; c < report.channels; ++c)
    {
        for (const float s : (c == 0 ? left : right))
        {
            if (! std::isfinite(s))
                continue;
            report.peak = std::max(report.peak, static_cast<double>(std::abs(s)));
            sumSquares += static_cast<double>(s) * s;
            ++counted;
            if (std::abs(s) >= 0.999f)
                ++report.clipped;
        }
    }
    report.rms = counted > 0 ? std::sqrt(sumSquares / static_cast<double>(counted)) : 0.0;
    for (const float s : mono)
        sumMono += s;
    report.dc = mono.empty() ? 0.0 : sumMono / static_cast<double>(mono.size());

    if (report.channels == 2 && ! left.empty() && left.size() == right.size())
    {
        double ll = 0.0, rr = 0.0, lr = 0.0;
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            if (! std::isfinite(left[i]) || ! std::isfinite(right[i]))
                continue;
            ll += static_cast<double>(left[i]) * left[i];
            rr += static_cast<double>(right[i]) * right[i];
            lr += static_cast<double>(left[i]) * right[i];
        }
        report.stereoCorrelation = ll > 0.0 && rr > 0.0 ? lr / std::sqrt(ll * rr) : 1.0;
        report.balanceDb = toDb(std::sqrt(rr)) - toDb(std::sqrt(ll));
        report.stereoImage = analyseStereoImage(left, right, sampleRate);
    }

    report.tempo = estimateTempo(mono, sampleRate);
    report.sectionMap = findSections(mono, sampleRate, report.stereoImage.seconds.empty() ? nullptr : &report.stereoImage);

    // Windows of 10 ms for the level over time, onsets and silence.
    const auto frame = std::max<std::size_t>(1, static_cast<std::size_t>(sampleRate * 0.010));
    std::vector<double> frameDb;
    for (std::size_t at = 0; at + frame <= mono.size(); at += frame)
    {
        double e = 0.0;
        for (std::size_t i = 0; i < frame; ++i)
            e += static_cast<double>(mono[at + i]) * mono[at + i];
        frameDb.push_back(toDb(std::sqrt(e / static_cast<double>(frame))));
    }

    if (! frameDb.empty())
    {
        const std::size_t perWindow = 5;   // 50 ms
        std::size_t quiet = 0, windows = 0;
        for (std::size_t i = 0; i + perWindow <= frameDb.size(); i += perWindow, ++windows)
        {
            double top = -200.0;
            for (std::size_t k = 0; k < perWindow; ++k)
                top = std::max(top, frameDb[i + k]);
            if (top < -80.0)
                ++quiet;
            if (report.firstSoundAt < 0.0 && top > -60.0)
                report.firstSoundAt = static_cast<double>(i) * 0.010;
        }
        report.silentFraction = windows > 0 ? static_cast<double>(quiet) / static_cast<double>(windows) : 1.0;

        // An onset: a frame well above what the previous fifty milliseconds held.
        double lastOnset = -1.0;
        for (std::size_t i = 1; i < frameDb.size() && report.onsets.size() < 64; ++i)
        {
            double before = -200.0;
            for (std::size_t k = i > 5 ? i - 5 : 0; k < i; ++k)
                before = std::max(before, frameDb[k]);
            const double t = static_cast<double>(i) * 0.010;
            if (frameDb[i] > -45.0 && frameDb[i] - before > 12.0 && (lastOnset < 0.0 || t - lastOnset > 0.08))
            {
                report.onsets.push_back(t);
                lastOnset = t;
            }
        }

        // A level for each quarter second.
        const std::size_t perStep = 25;
        for (std::size_t i = 0; i < frameDb.size() && report.envelopeDb.size() < 80; i += perStep)
        {
            const std::size_t end = std::min(frameDb.size(), i + perStep);
            double power = 0.0;
            for (std::size_t k = i; k < end; ++k)
                power += std::pow(10.0, frameDb[k] / 10.0);
            report.envelopeDb.push_back(10.0 * std::log10(std::max(power / static_cast<double>(end - i), 1.0e-12)));
        }
    }

    // Spectrum: averaged Hann-windowed frames, then octave bands, centroid and rolloff.
    constexpr int order = 12;
    constexpr int size = 1 << order;
    report.bandsDb.assign(9, -120.0);
    if (mono.size() >= static_cast<std::size_t>(size))
    {
        juce::dsp::FFT fft(order);
        juce::dsp::WindowingFunction<float> window(size, juce::dsp::WindowingFunction<float>::hann);
        std::vector<double> power(size / 2, 0.0);

        const std::size_t frames = (mono.size() - size) / (size / 2) + 1;
        const std::size_t every = std::max<std::size_t>(1, frames / 64);   // at most about 64 frames
        std::size_t used = 0;
        std::vector<float> buffer(static_cast<std::size_t>(size) * 2);

        for (std::size_t f = 0; f < frames; f += every)
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            std::copy_n(mono.begin() + static_cast<std::ptrdiff_t>(f * (size / 2)), size, buffer.begin());
            window.multiplyWithWindowingTable(buffer.data(), size);
            fft.performFrequencyOnlyForwardTransform(buffer.data());
            for (int b = 1; b < size / 2; ++b)
                power[static_cast<std::size_t>(b)] += static_cast<double>(buffer[static_cast<std::size_t>(b)]) * buffer[static_cast<std::size_t>(b)];
            ++used;
        }

        const double totalPower = std::accumulate(power.begin(), power.end(), 0.0);
        if (totalPower > 0.0 && used > 0)
        {
            // Bands are shares of the total mean square, so their levels are in dBFS RMS.
            double meanSquare = 0.0;
            for (const float s : mono)
                meanSquare += static_cast<double>(s) * s;
            meanSquare /= static_cast<double>(mono.size());

            double weighted = 0.0, running = 0.0, high = 0.0;
            bool rolled = false;
            for (int b = 1; b < size / 2; ++b)
            {
                const double hz = b * sampleRate / size;
                const double p = power[static_cast<std::size_t>(b)];
                weighted += hz * p;
                running += p;
                if (hz >= 2000.0)
                    high += p;
                if (! rolled && running >= 0.85 * totalPower)
                {
                    report.rolloffHz = hz;
                    rolled = true;
                }
                for (int band = 0; band < 9; ++band)
                {
                    const double centre = 63.0 * std::pow(2.0, band);
                    if (hz >= centre / std::sqrt(2.0) && hz < centre * std::sqrt(2.0))
                    {
                        const double share = p / totalPower;
                        const double previous = std::pow(10.0, report.bandsDb[static_cast<std::size_t>(band)] / 10.0);
                        report.bandsDb[static_cast<std::size_t>(band)] =
                            10.0 * std::log10(std::max(previous + share * meanSquare, 1.0e-12));
                    }
                }
            }
            report.centroidHz = weighted / totalPower;
            report.highShare = high / totalPower;
        }
    }

    report.pitch = estimatePitch(mono, sampleRate);

    // -- what to say about it -----------------------------------------------------------
    auto& notes = report.notes;
    if (report.nonFinite > 0)
        notes.push_back(std::to_string(report.nonFinite) + " samples are not numbers (NaN or infinity): something in the circuit is unstable.");
    if (report.peak < 1.0e-5)
        notes.push_back("Silent. Check that the output is wired, that a gate reaches the envelope (play a note), and that no control arc is overwriting a value it should not.");
    else
    {
        if (report.clipped > 0 || report.peak > 1.0)
            notes.push_back("Clips: peak is " + number(toDb(report.peak)) + " dBFS (" + std::to_string(report.clipped) + " samples at full scale). Lower a gain or add a limiter.");
        if (toDb(report.rms) < -50.0)
            notes.push_back("Very quiet: RMS is " + number(toDb(report.rms)) + " dBFS.");
        if (std::abs(report.dc) > 0.01)
            notes.push_back("DC offset of " + number(report.dc) + ": add a high-pass or check for an unbalanced stage.");
        if (report.firstSoundAt > 0.25)
            notes.push_back("Nothing sounds for the first " + number(report.firstSoundAt) + " s.");
        if (report.silentFraction > 0.9)
            notes.push_back("Silent for most of the render (" + number(100.0 * report.silentFraction) + "%).");
        if (report.channels == 2 && report.stereoCorrelation < -0.5)
            notes.push_back("The channels are close to opposite in phase and will cancel in mono.");
        for (const auto& a : report.stereoImage.anomalies)
            notes.push_back("Stereo image changes from " + number(a.startSeconds) + " s to " + number(a.endSeconds) + " s: " + a.reason + ".");
        // A bass or a pad is meant to be dark; anything else with so little up here will not cut through a mix.
        const bool lowSound = report.pitch.found && report.pitch.hz < 300.0;
        if (report.highShare < 0.03 && report.rms > 1.0e-4 && ! lowSound)
            notes.push_back("Dark: only " + number(100.0 * report.highShare) + "% of the power is above 2 kHz. Fine for a bass or a pad; "
                            "a hat, a snare, a lead or metal that should be heard will be buried.");
        if (report.pitch.found)
            notes.push_back("Pitched: " + number(report.pitch.hz) + " Hz, " + noteName(report.pitch.midiNote)
                            + (report.pitch.cents >= 0 ? " +" : " ") + number(report.pitch.cents) + " cents.");
        else
            notes.push_back("No clear pitch: noise, a chord, or a sound too short or too unsteady to name a note.");
    }

    return report;
}

std::string reportToJson(const SignalReport& r)
{
    std::string json = "{";
    json += "\"seconds\":" + number(r.seconds);
    json += ",\"sampleRate\":" + number(r.sampleRate);
    json += ",\"channels\":" + std::to_string(r.channels);
    json += ",\"peak\":" + number(r.peak) + ",\"peakDb\":" + number(toDb(r.peak));
    json += ",\"rms\":" + number(r.rms) + ",\"rmsDb\":" + number(toDb(r.rms));
    json += ",\"dc\":" + number(r.dc);
    json += ",\"clipped\":" + std::to_string(r.clipped);
    json += ",\"nonFinite\":" + std::to_string(r.nonFinite);
    json += ",\"silentFraction\":" + number(r.silentFraction);
    json += ",\"firstSoundAt\":" + number(r.firstSoundAt);

    json += ",\"pitch\":{\"found\":" + std::string(r.pitch.found ? "true" : "false");
    if (r.pitch.found)
        json += ",\"hz\":" + number(r.pitch.hz) + ",\"note\":" + quote(noteName(r.pitch.midiNote))
              + ",\"cents\":" + number(r.pitch.cents) + ",\"voicedFraction\":" + number(r.pitch.voicedFraction)
              + ",\"stability\":" + number(r.pitch.stability);
    json += "}";

    json += ",\"bandCentresHz\":[63,126,252,504,1008,2016,4032,8064,16128]";
    json += ",\"bandsDb\":" + array(r.bandsDb);
    json += ",\"centroidHz\":" + number(r.centroidHz) + ",\"rolloffHz\":" + number(r.rolloffHz)
          + ",\"highShare\":" + number(r.highShare);
    json += ",\"onsets\":" + array(r.onsets);
    json += ",\"envelopeDbPerQuarterSecond\":" + array(r.envelopeDb);
    if (r.channels == 2)
    {
        json += ",\"stereoCorrelation\":" + number(r.stereoCorrelation) + ",\"balanceDb\":" + number(r.balanceDb);
        const auto& image = r.stereoImage;
        if (image.validSeconds > 0)
        {
            // A second too quiet to measure is null. The arrays stop at 300 seconds.
            std::vector<double> widths, correlations;
            for (std::size_t s = 0; s < image.seconds.size() && s < 300; ++s)
            {
                const double none = std::nan("");
                widths.push_back(image.seconds[s].valid ? image.seconds[s].widthDb : none);
                correlations.push_back(image.seconds[s].valid ? image.seconds[s].correlation : none);
            }
            json += ",\"stereoImage\":{\"medianWidthDb\":" + number(image.medianWidthDb)
                  + ",\"medianCorrelation\":" + number(image.medianCorrelation)
                  + ",\"bandWidthDb\":{\"low\":" + number(image.lowDb) + ",\"mid\":" + number(image.midDb)
                  + ",\"high\":" + number(image.highDb) + "}"
                  + ",\"widthDbPerSecond\":" + array(widths) + ",\"correlationPerSecond\":" + array(correlations)
                  + ",\"anomalies\":[";
            for (std::size_t i = 0; i < image.anomalies.size(); ++i)
                json += std::string(i ? "," : "") + "{\"startS\":" + number(image.anomalies[i].startSeconds)
                      + ",\"endS\":" + number(image.anomalies[i].endSeconds)
                      + ",\"reason\":" + quote(image.anomalies[i].reason) + "}";
            json += "]}";
        }
    }

    if (r.tempo.found)
    {
        json += ",\"tempo\":{\"bpm\":" + number(r.tempo.bpm()) + ",\"candidates\":[";
        for (std::size_t i = 0; i < r.tempo.candidates.size(); ++i)
            json += std::string(i ? "," : "") + "{\"bpm\":" + number(r.tempo.candidates[i].bpm)
                  + ",\"strength\":" + number(r.tempo.candidates[i].strength) + "}";
        json += "]}";
    }

    if (! r.sectionMap.sections.empty())
    {
        json += ",\"sections\":[";
        for (std::size_t i = 0; i < r.sectionMap.sections.size(); ++i)
        {
            const auto& s = r.sectionMap.sections[i];
            json += std::string(i ? "," : "") + "{\"startS\":" + number(s.startSeconds) + ",\"endS\":" + number(s.endSeconds)
                  + ",\"energyDb\":" + number(s.energyDb) + ",\"dominantNote\":" + quote(pitchClassName(s.dominantPitchClass))
                  + ",\"key\":" + quote(keyName(s.key)) + ",\"keyCorrelation\":" + number(s.key.correlation);
            if (s.hasStereo)
                json += ",\"widthDb\":" + number(s.widthDb) + ",\"correlation\":" + number(s.correlation);
            json += "}";
        }
        json += "]";
        json += ",\"key\":" + quote(keyName(r.sectionMap.key)) + ",\"keyCorrelation\":" + number(r.sectionMap.key.correlation);
    }

    json += ",\"notes\":[";
    for (std::size_t i = 0; i < r.notes.size(); ++i)
        json += (i ? "," : "") + quote(r.notes[i]);
    json += "]}";
    return json;
}

}  // namespace valis

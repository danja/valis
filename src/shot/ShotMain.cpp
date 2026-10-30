// src/shot/ShotMain.cpp
//
// valis-shot - listening for a caller that cannot hear. Ideas ported from earshot
// (https://github.com/Leonxlnx/earshot), which gives coding agents measurements of a sound in place of ears.
//
//   valis-shot measure file.wav        one JSON object: signal, loudness and envelope
//   valis-shot compare a.wav b.wav     how far apart two renders are, in dB, and how their loudness differs
//   valis-shot listen file.wav [QUERY...]  what the sound is (AudioSet labels) and how well each sentence fits it (CLAP);
//                                      needs -DVALIS_WITH_ONNX=ON and scripts/shot/fetch-models.sh

#include "valis/Analysis.h"
#include "valis/EnvelopeTimes.h"
#include "valis/Loudness.h"
#include "valis/NeuralEars.h"
#include "valis/SpectralDistance.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Audio
{
    std::vector<float> left, right;
    double rate = 0.0;
};

bool load(const std::string& path, Audio& out)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(path)));
    if (reader == nullptr)
        return false;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    out.rate = reader->sampleRate;
    out.left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + buffer.getNumSamples());
    if (buffer.getNumChannels() > 1)
        out.right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + buffer.getNumSamples());
    return true;
}

std::vector<float> mono(const Audio& a)
{
    std::vector<float> m = a.left;
    if (! a.right.empty())
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (m[i] + a.right[i]);
    return m;
}

int measure(const std::string& path)
{
    Audio a;
    if (! load(path, a))
    {
        std::fprintf(stderr, "valis-shot: cannot read %s\n", path.c_str());
        return 1;
    }
    const auto signal = valis::analyseSignal(a.left, a.right, a.rate);
    const auto loud = valis::measureLoudness(a.left, a.right, a.rate);
    const auto env = valis::measureEnvelope(mono(a), a.rate);
    std::printf("{\"file\":\"%s\",\"signal\":%s,\n \"loudness\":{\"integratedLufs\":%.2f,\"rangeLu\":%.2f,"
                "\"shortTermMaxLufs\":%.2f,\"momentaryMaxLufs\":%.2f,\"truePeakDbtp\":%.2f},\n"
                " \"envelope\":{\"attackAtMs\":%.0f,\"peakAtMs\":%.0f,\"attackMs\":%.0f,\"decayMs\":%.0f,"
                "\"noiseFloorDbfs\":%.1f,\"dynamicRangeDb\":%.1f}}\n",
                path.c_str(), valis::reportToJson(signal).c_str(), loud.integratedLufs, loud.rangeLu, loud.shortTermMaxLufs,
                loud.momentaryMaxLufs, loud.truePeakDbtp, env.attackAtMs, env.peakAtMs, env.attackMs, env.decayMs,
                env.noiseFloorDbfs, env.dynamicRangeDb);
    return 0;
}

int compare(const std::string& pathA, const std::string& pathB)
{
    Audio a, b;
    if (! load(pathA, a) || ! load(pathB, b))
    {
        std::fprintf(stderr, "valis-shot: cannot read both files\n");
        return 1;
    }
    if (a.rate != b.rate)
    {
        std::fprintf(stderr, "valis-shot: sample rates differ (%.0f and %.0f)\n", a.rate, b.rate);
        return 1;
    }
    const auto d = valis::spectralDistance(mono(a), mono(b), a.rate);
    const auto la = valis::measureLoudness(a.left, a.right, a.rate);
    const auto lb = valis::measureLoudness(b.left, b.right, b.rate);
    std::printf("{\"spectralDistanceDb\":%.2f,\"perResolution\":[%.2f,%.2f,%.2f],"
                "\"loudnessDifferenceLu\":%.2f,\"lengthDifferenceSeconds\":%.3f}\n",
                d.meanDb, d.perResolution[0], d.perResolution[1], d.perResolution[2], lb.integratedLufs - la.integratedLufs,
                (static_cast<double>(b.left.size()) - static_cast<double>(a.left.size())) / a.rate);
    return 0;
}

#ifdef VALIS_WITH_ONNX
/// The loudest ten seconds (by whole-second RMS), since AST reads only one window and a long file often opens quietly.
std::vector<float> loudestWindow(const std::vector<float>& x, double rate)
{
    const std::size_t window = static_cast<std::size_t>(10.0 * rate), hop = static_cast<std::size_t>(rate);
    if (x.size() <= window)
        return x;
    std::size_t best = 0;
    double bestEnergy = -1.0;
    for (std::size_t start = 0; start + window <= x.size() + hop; start += hop)
    {
        const std::size_t end = std::min(x.size(), start + window);
        double energy = 0.0;
        for (std::size_t i = start; i < end; ++i)
            energy += static_cast<double>(x[i]) * x[i];
        if (energy > bestEnergy) { bestEnergy = energy; best = start; }
    }
    return std::vector<float>(x.begin() + static_cast<long>(best), x.begin() + static_cast<long>(std::min(x.size(), best + window)));
}

int listen(const std::string& path, const std::vector<std::string>& queries)
{
    Audio a;
    if (! load(path, a))
    {
        std::fprintf(stderr, "valis-shot: cannot read %s\n", path.c_str());
        return 1;
    }
    valis::NeuralEars ears;
    std::string error;
    if (! ears.load(valis::NeuralEars::defaultModelsDir(), error))
    {
        std::fprintf(stderr, "valis-shot: %s\n", error.c_str());
        return 1;
    }
    const auto sound = mono(a);
    std::printf("{\"file\":\"%s\"", path.c_str());
    if (ears.hasAst())
    {
        std::printf(",\"tags\":[");
        const auto tags = ears.tags(loudestWindow(sound, a.rate), a.rate, 8);
        for (std::size_t i = 0; i < tags.size(); ++i)
            std::printf("%s{\"label\":\"%s\",\"probability\":%.3f}", i ? "," : "", tags[i].label.c_str(), tags[i].probability);
        std::printf("]");
    }
    if (ears.hasClap() && ! queries.empty())
    {
        const auto embedding = ears.embedAudio(sound, a.rate);
        std::vector<float> cosines;
        for (const auto& q : queries)
            cosines.push_back(valis::cosine(embedding, ears.embedText(q)));
        // exp(logit_scale_a) of laion/larger_clap_general (stored 3.65493), read from its safetensors header.
        constexpr double kClapTemperature = 38.6647;
        const float top = *std::max_element(cosines.begin(), cosines.end());
        double total = 0.0;
        for (float c : cosines)
            total += std::exp(kClapTemperature * (c - top));
        std::printf(",\"matches\":[");
        for (std::size_t i = 0; i < queries.size(); ++i)
            std::printf("%s{\"query\":\"%s\",\"cosine\":%.3f,\"share\":%.3f}", i ? "," : "", queries[i].c_str(), cosines[i],
                        std::exp(kClapTemperature * (cosines[i] - top)) / total);
        std::printf("]");
    }
    std::printf("}\n");
    return 0;
}
#endif

}  // namespace

int main(int argc, char** argv)
{
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "measure" && argc == 3)
        return measure(argv[2]);
    if (cmd == "compare" && argc == 4)
        return compare(argv[2], argv[3]);
#ifdef VALIS_WITH_ONNX
    if (cmd == "listen" && argc >= 3)
        return listen(argv[2], std::vector<std::string>(argv + 3, argv + argc));
#endif
    std::fprintf(stderr, "usage: valis-shot measure FILE | compare A B | listen FILE [QUERY...]\n");
    return 2;
}

// src/shot/ShotReport.cpp

#include "valis/ShotReport.h"

#include "valis/Analysis.h"
#include "valis/EnvelopeTimes.h"
#include "valis/Loudness.h"
#include "valis/SpectralDistance.h"

#ifdef VALIS_WITH_ONNX
 #include "valis/NeuralEars.h"
#endif

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace valis {
namespace {

struct Audio
{
    std::vector<float> left, right;
    double rate = 0.0;
};

std::string load(const std::string& path, Audio& out)
{
    if (path.empty())
        return "a path to an audio file is needed";
    const juce::String text(path);
    const juce::File file = juce::File::isAbsolutePath(text) ? juce::File(text)
                                                             : juce::File::getCurrentWorkingDirectory().getChildFile(text);
    if (! file.existsAsFile())
        return "no such file: " + file.getFullPathName().toStdString();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return "not an audio file this build can read: " + file.getFullPathName().toStdString();

    const auto frames = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples,
                                                               static_cast<juce::int64>(reader->sampleRate * 600.0)));
    if (frames <= 0)
        return "the file holds no audio";

    juce::AudioBuffer<float> buffer(static_cast<int>(std::min<unsigned>(reader->numChannels, 2u)), frames);
    reader->read(&buffer, 0, frames, 0, true, buffer.getNumChannels() > 1);
    out.rate = reader->sampleRate;
    out.left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + frames);
    if (buffer.getNumChannels() > 1)
        out.right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + frames);
    return {};
}

std::vector<float> mono(const Audio& a)
{
    std::vector<float> m = a.left;
    if (! a.right.empty())
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (m[i] + a.right[i]);
    return m;
}

ShotResult failure(std::string message)
{
    ShotResult r;
    r.error = std::move(message);
    return r;
}

ShotResult success(std::string json)
{
    ShotResult r;
    r.ok = true;
    r.json = std::move(json);
    return r;
}

/// A JSON string with the quotes and backslashes in a file name or a sentence escaped.
std::string quote(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
            out += {'\\', c};
        else if (static_cast<unsigned char>(c) < 0x20)
            out += ' ';
        else
            out += c;
    }
    return out + "\"";
}

/// A number to a fixed count of places, without trailing noise; a value that is not finite reads as 0.
std::string num(double v, int places)
{
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", places, std::isfinite(v) ? v : 0.0);
    return text;
}

#ifdef VALIS_WITH_ONNX
/// The loudest ten seconds (by whole-second energy), since AST reads one window and a long file often opens quietly.
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
    return std::vector<float>(x.begin() + static_cast<long>(best),
                              x.begin() + static_cast<long>(std::min(x.size(), best + window)));
}

/// One set of models for the process: loading them takes seconds and about a gigabyte.
NeuralEars* sharedEars(std::string& error)
{
    static NeuralEars ears;
    static bool tried = false, loaded = false;
    static std::string reason;
    if (! tried)
    {
        tried = true;
        loaded = ears.load(NeuralEars::defaultModelsDir(), reason);
    }
    error = reason;
    return loaded ? &ears : nullptr;
}
#endif

}  // namespace

ShotResult shotMeasure(const std::string& path)
{
    Audio a;
    if (const auto error = load(path, a); ! error.empty())
        return failure(error);

    const auto signal = reportToJson(analyseSignal(a.left, a.right, a.rate));
    const auto loud = measureLoudness(a.left, a.right, a.rate);
    const auto env = measureEnvelope(mono(a), a.rate);
    return success("{\"file\":" + quote(path) + ",\"signal\":" + signal
                   + ",\"loudness\":{\"integratedLufs\":" + num(loud.integratedLufs, 2) + ",\"rangeLu\":" + num(loud.rangeLu, 2)
                   + ",\"shortTermMaxLufs\":" + num(loud.shortTermMaxLufs, 2) + ",\"momentaryMaxLufs\":"
                   + num(loud.momentaryMaxLufs, 2) + ",\"truePeakDbtp\":" + num(loud.truePeakDbtp, 2) + "}"
                   + ",\"envelope\":{\"attackAtMs\":" + num(env.attackAtMs, 0) + ",\"peakAtMs\":" + num(env.peakAtMs, 0)
                   + ",\"attackMs\":" + num(env.attackMs, 0) + ",\"decayMs\":" + num(env.decayMs, 0)
                   + ",\"noiseFloorDbfs\":" + num(env.noiseFloorDbfs, 1) + ",\"dynamicRangeDb\":" + num(env.dynamicRangeDb, 1) + "}}");
}

ShotResult shotCompare(const std::string& pathA, const std::string& pathB)
{
    Audio a, b;
    if (const auto error = load(pathA, a); ! error.empty())
        return failure(error);
    if (const auto error = load(pathB, b); ! error.empty())
        return failure(error);
    if (std::abs(a.rate - b.rate) > 0.5)
        return failure("sample rates differ (" + std::to_string(static_cast<int>(a.rate)) + " and "
                       + std::to_string(static_cast<int>(b.rate)) + ")");

    const auto d = spectralDistance(mono(a), mono(b), a.rate);
    const auto la = measureLoudness(a.left, a.right, a.rate);
    const auto lb = measureLoudness(b.left, b.right, b.rate);
    return success("{\"spectralDistanceDb\":" + num(d.meanDb, 2) + ",\"perResolution\":[" + num(d.perResolution[0], 2) + ","
                   + num(d.perResolution[1], 2) + "," + num(d.perResolution[2], 2) + "],\"loudnessDifferenceLu\":"
                   + num(lb.integratedLufs - la.integratedLufs, 2) + ",\"lengthDifferenceSeconds\":"
                   + num((static_cast<double>(b.left.size()) - static_cast<double>(a.left.size())) / a.rate, 3) + "}");
}

bool shotCanListen(std::string& reason)
{
#ifdef VALIS_WITH_ONNX
    return sharedEars(reason) != nullptr;
#else
    reason = "this build has no neural models: configure with -DVALIS_WITH_ONNX=ON, then run scripts/shot/fetch-models.sh";
    return false;
#endif
}

#ifdef VALIS_WITH_ONNX
namespace {

/// CLAP's cosines turned into shares among the sentences given. The temperature is exp(logit_scale_a) of
/// laion/larger_clap_general (stored 3.65493), read from its safetensors header.
std::vector<double> shares(const std::vector<float>& cosines)
{
    constexpr double kClapTemperature = 38.6647;
    const float top = *std::max_element(cosines.begin(), cosines.end());
    std::vector<double> out;
    double total = 0.0;
    for (float c : cosines)
        total += out.emplace_back(std::exp(kClapTemperature * (c - top)));
    for (auto& v : out)
        v /= total;
    return out;
}

}  // namespace
#endif

ShotResult shotListen(const std::string& path, const std::vector<std::string>& queries)
{
#ifdef VALIS_WITH_ONNX
    Audio a;
    if (const auto error = load(path, a); ! error.empty())
        return failure(error);
    std::string error;
    NeuralEars* ears = sharedEars(error);
    if (ears == nullptr)
        return failure(error);

    const auto sound = mono(a);
    std::string json = "{\"file\":" + quote(path);
    if (ears->hasAst())
    {
        json += ",\"tags\":[";
        const auto tags = ears->tags(loudestWindow(sound, a.rate), a.rate, 8);
        for (std::size_t i = 0; i < tags.size(); ++i)
            json += std::string(i ? "," : "") + "{\"label\":" + quote(tags[i].label) + ",\"probability\":" + num(tags[i].probability, 3) + "}";
        json += "]";
    }
    if (ears->hasClap() && ! queries.empty())
    {
        const auto embedding = ears->embedAudio(sound, a.rate);
        std::vector<float> cosines;
        for (const auto& q : queries)
            cosines.push_back(cosine(embedding, ears->embedText(q)));
        const auto share = shares(cosines);
        json += ",\"matches\":[";
        for (std::size_t i = 0; i < queries.size(); ++i)
            json += std::string(i ? "," : "") + "{\"query\":" + quote(queries[i]) + ",\"cosine\":" + num(cosines[i], 3)
                  + ",\"share\":" + num(share[i], 3) + "}";
        json += "]";
    }
    return success(json + "}");
#else
    (void) path;
    (void) queries;
    std::string reason;
    shotCanListen(reason);
    return failure(reason);
#endif
}

ShotResult shotIs(const std::string& path, const std::string& sentence)
{
    if (sentence.empty())
        return failure("a sentence is needed, such as \"a plucked nylon guitar\"");
#ifdef VALIS_WITH_ONNX
    Audio a;
    if (const auto error = load(path, a); ! error.empty())
        return failure(error);
    std::string error;
    NeuralEars* ears = sharedEars(error);
    if (ears == nullptr || ! ears->hasClap())
        return failure(ears == nullptr ? error : "the CLAP model is not installed: run scripts/shot/fetch-models.sh");

    // What a sound is most often mistaken for. The sentence competes with these, so its share means something.
    static const std::vector<std::string> rivals = {
        "a plucked nylon string guitar", "an electric guitar", "a bass guitar", "a harp", "a piano", "a violin section",
        "a cello", "a flute", "a clarinet", "a saxophone", "a trumpet", "an electric organ", "a synthesizer keyboard preset",
        "a drum kit", "conga drums", "maracas shaking", "a bell", "a human voice singing", "white noise", "a pure sine tone"};
    std::vector<std::string> queries = {sentence};
    for (const auto& r : rivals)
        if (r != sentence)
            queries.push_back(r);

    const auto embedding = ears->embedAudio(mono(a), a.rate);
    std::vector<float> cosines;
    for (const auto& q : queries)
        cosines.push_back(cosine(embedding, ears->embedText(q)));
    const auto share = shares(cosines);

    // Closest first, the sentence itself included wherever it falls.
    std::vector<std::size_t> order(queries.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return share[x] > share[y]; });
    const auto rank = static_cast<std::size_t>(std::find(order.begin(), order.end(), std::size_t{0}) - order.begin()) + 1;

    // A judgement, not a measurement: the cut-offs are chosen so a clear winner reads yes.
    const char* verdict = share[0] >= 0.5 ? "yes" : share[0] >= 0.2 ? "maybe" : "no";
    std::string json = "{\"file\":" + quote(path) + ",\"sentence\":" + quote(sentence) + ",\"verdict\":\"" + verdict
                     + "\",\"share\":" + num(share[0], 3) + ",\"rank\":" + std::to_string(rank) + ",\"of\":"
                     + std::to_string(queries.size()) + ",\"closest\":[";
    for (std::size_t i = 0; i < 3 && i < order.size(); ++i)
        json += std::string(i ? "," : "") + "{\"sentence\":" + quote(queries[order[i]]) + ",\"share\":" + num(share[order[i]], 3) + "}";
    return success(json + "]}");
#else
    (void) path;
    std::string reason;
    shotCanListen(reason);
    return failure(reason);
#endif
}

}  // namespace valis

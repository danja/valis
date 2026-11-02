// src/ops/SongRender.cpp
#include "valis/SongRender.h"

#include "valis/OfflineRender.h"

#include <algorithm>
#include <atomic>
#include <thread>

#include <juce_core/juce_core.h>

namespace valis {

namespace {

std::string under(const std::string& directory, const std::string& relative)
{
    return juce::File::isAbsolutePath(juce::String(relative))
               ? relative
               : juce::File(juce::String(directory)).getChildFile(juce::String(relative)).getFullPathName().toStdString();
}

/// Runs `work(i)` for i in [0, count) on up to `workers` threads.
template <typename Work>
void forEachParallel(std::size_t count, int workers, Work work)
{
    std::atomic<std::size_t> next{0};
    const auto run = [&]
    {
        for (auto i = next++; i < count; i = next++)
            work(i);
    };
    std::vector<std::thread> pool;
    const auto n = std::min<std::size_t>(static_cast<std::size_t>(std::max(1, workers)), count);
    for (std::size_t t = 1; t < n; ++t)
        pool.emplace_back(run);
    run();
    for (auto& t : pool)
        t.join();
}

} // namespace

SongRenderResult renderSongStems(const Ontology& ontology, const ElementRegistry& registry, const Song& song,
                                 const std::string& songDirectory, const std::string& wavDirectory,
                                 const SongRenderOptions& options)
{
    const auto wanted = [&](const SongStem& s)
    {
        if (s.recorded)
            return false;
        const auto& part = s.isReturn ? s.source : s.name;
        return options.only.empty() || std::find(options.only.begin(), options.only.end(), part) != options.only.end();
    };

    juce::File(juce::String(wavDirectory)).createDirectory();

    SongRenderResult result;
    std::vector<const SongStem*> parts, returns;
    for (const auto& s : song.stems)
        if (wanted(s))
            (s.isReturn ? returns : parts).push_back(&s);

    result.stems.resize(parts.size() + returns.size());

    const auto render = [&](const SongStem& stem, SongRenderEntry& entry)
    {
        entry.name = stem.name;
        entry.path = wavDirectory + "/" + stem.name + ".wav";

        OfflineRenderRequest request;
        request.circuitPath = under(songDirectory, stem.circuit);
        request.sampleRate = options.sampleRate;
        request.tempo = song.tempo;
        if (stem.isReturn)
        {
            request.inputPath = wavDirectory + "/" + stem.source + ".wav";
            if (! stem.midi.empty())            // an effect that is played, such as a rotating speaker's speed
            {
                request.midiPath = under(songDirectory, stem.midi);
                request.rolling = true;
            }
        }
        else
        {
            request.midiPath = under(songDirectory, stem.midi);
            request.rolling = true;
            request.seconds = song.renderSeconds();
        }

        auto rendered = renderCircuitFile(ontology, registry, request);
        if (! rendered.ok)
        {
            entry.error = rendered.error;
            return;
        }
        if (rendered.nonFinite > 0)
        {
            entry.error = request.circuitPath + ": " + std::to_string(rendered.nonFinite)
                        + " samples are NaN or infinite, so the stem was not written";
            return;
        }
        std::string error;
        if (! writeWavFile(entry.path, rendered.audio, error))
        {
            entry.error = error;
            return;
        }
        entry.ok = true;
        entry.seconds = static_cast<double>(rendered.audio.frames()) / rendered.audio.sampleRate;
        entry.peak = rendered.peak;
        entry.rms = rendered.rms;
        entry.clipped = rendered.peak > 1.0;
    };

    // Parts first, since a return reads its source part's wav.
    forEachParallel(parts.size(), options.workers, [&](std::size_t i) { render(*parts[i], result.stems[i]); });
    forEachParallel(returns.size(), options.workers,
                    [&](std::size_t i) { render(*returns[i], result.stems[parts.size() + i]); });

    result.ok = ! result.stems.empty() && std::all_of(result.stems.begin(), result.stems.end(),
                                                      [](const SongRenderEntry& e) { return e.ok; });
    return result;
}

} // namespace valis

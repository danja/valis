// src/ops/OfflineRender.cpp
#include "valis/OfflineRender.h"

#include "valis/CircuitCompiler.h"
#include "valis/DspElement.h"
#include "valis/MidiScore.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"

#include <algorithm>
#include <cmath>

namespace valis {

OfflineRenderResult renderCircuitFile(const Ontology& ontology, const ElementRegistry& registry,
                                      const OfflineRenderRequest& request)
{
    OfflineRenderResult result;
    const auto fail = [&](const std::string& message)
    {
        result.error = request.circuitPath + ": " + message;
        return result;
    };

    const double rate = std::clamp(request.sampleRate, 8000.0, 192000.0);
    const double tempo = std::clamp(request.tempo, 20.0, 400.0);
    const int block = std::clamp(request.blockSize, 16, 4096);

    // -- the circuit, on a private copy of everything
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    if (! store.parseFile(request.circuitPath, parseErrors))
    {
        std::string message = "does not parse";
        for (const auto& e : parseErrors)
            message += "\n  " + e.toString();
        return fail(message);
    }

    CircuitModel model;
    if (! model.build(store, ontology, result.diagnostics))
        return fail("will not build: " + (result.diagnostics.empty() ? std::string("no reason given")
                                                                     : result.diagnostics.front().message));

    CompiledCircuit compiled;
    CircuitCompiler compiler;
    if (! compiler.compile(model, ontology, compiled, result.diagnostics))
        return fail("will not compile: " + (result.diagnostics.empty() ? std::string("no reason given")
                                                                       : result.diagnostics.front().message));

    ValisEngine engine;
    engine.prepare(rate, block);
    std::string error;
    if (! engine.load(compiled, registry, error))
        return fail("will not load: " + error);
    result.nodes = compiled.nodes.size();

    // -- what plays it
    std::vector<float> input;
    if (! request.inputPath.empty())
    {
        StereoAudio source;
        if (! readAudioFile(request.inputPath, source, error))
            return fail(error);
        if (std::abs(source.sampleRate - rate) > 0.5)
            return fail("the input " + request.inputPath + " is at " + std::to_string(source.sampleRate)
                        + " Hz, not " + std::to_string(rate));
        input.resize(source.frames());
        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = 0.5f * (source.left[i] + source.right[i]);
    }
    else
        input.assign(static_cast<std::size_t>(std::clamp(request.seconds, 0.05, 1200.0) * rate), 0.0f);

    std::vector<TimedNote> score;
    if (! request.midiPath.empty() && ! readMidiScore(request.midiPath, tempo, rate, score, error))
        return fail(error);

    result.audio.sampleRate = rate;
    result.audio.left.assign(input.size(), 0.0f);
    result.audio.right.assign(input.size(), 0.0f);

    // -- the blocks. An event fires in the block that contains it, at its own offset.
    std::size_t nextEvent = 0;
    for (std::size_t at = 0; at < input.size(); at += static_cast<std::size_t>(block))
    {
        const auto n = static_cast<int>(std::min(static_cast<std::size_t>(block), input.size() - at));
        const auto sample = static_cast<long long>(at);

        while (nextEvent < score.size() && score[nextEvent].sample < sample + n)
        {
            const auto& e = score[nextEvent++];
            const int offset = static_cast<int>(std::max<long long>(0, e.sample - sample));
            if (e.on) engine.queueNoteOn(e.note, e.velocity, offset);
            else      engine.queueNoteOff(e.note, offset);
        }

        TransportInfo transport;
        transport.playing = request.rolling;
        transport.tempoBpm = tempo;
        transport.ppqPosition = request.rolling ? static_cast<double>(at) / rate * tempo / 60.0 : 0.0;
        engine.setTransport(transport);

        engine.process(input.data() + at, result.audio.left.data() + at, result.audio.right.data() + at, n);
    }

    double sum = 0.0;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const float s = 0.5f * (result.audio.left[i] + result.audio.right[i]);
        if (! std::isfinite(s))
        {
            ++result.nonFinite;
            continue;
        }
        result.peak = std::max(result.peak, static_cast<double>(std::abs(s)));
        sum += static_cast<double>(s) * s;
    }
    result.rms = input.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(input.size()));
    result.ok = true;
    return result;
}

} // namespace valis

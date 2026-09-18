// src/engine/ValisEngine.cpp

#include "valis/ValisEngine.h"

#include "valis/Ontology.h"

#include "dsp/Oversampled.h"

#include <algorithm>
#include <cstring>

namespace valis {

ValisEngine::ValisEngine() = default;

ValisEngine::~ValisEngine()
{
    delete active.exchange(nullptr, std::memory_order_acq_rel);
    for (const auto& retired : graveyard)
        delete retired.graph;
}

void ValisEngine::prepare(double rate, int blockSize)
{
    sampleRate   = rate;
    maxBlockSize = std::max(blockSize, 1);
}

bool ValisEngine::load(const CompiledCircuit& circuit,
                       const ElementRegistry& registry,
                       std::string& error)
{
    if (! circuit.isValid())
    {
        error = "circuit is not valid";
        return false;
    }

    auto graph = std::make_unique<Graph>();
    graph->circuit   = circuit;
    graph->blockSize = maxBlockSize;

    // Everything below allocates. That is why it happens here and not in
    // process().
    graph->audio.assign(static_cast<std::size_t>(circuit.numBuffers) *
                        static_cast<std::size_t>(maxBlockSize), 0.0f);
    graph->controlStore.assign(static_cast<std::size_t>(std::max(circuit.numControlSlots, 1)), 0.0f);

    std::size_t maxAudioIn = 1, maxAudioOut = 1, maxControlIn = 1, maxControlOut = 1;

    graph->elements.reserve(circuit.nodes.size());
    graph->controlValues.reserve(circuit.nodes.size());

    int latency = 0;

    for (const auto& node : circuit.nodes)
    {
        auto element = registry.create(node.implementation);
        if (element == nullptr)
        {
            error = "no implementation registered for " + node.implementation;
            return false;
        }

        // val:oversampling is read here rather than in setOption, because the
        // factor has to be known before the element is prepared: the element
        // inside is prepared for the faster rate it will actually run at.
        for (const auto& [key, value] : node.options)
        {
            if (key != "oversampling")
                continue;

            int factor = 1;
            std::string reason;
            if (! Oversampled::parseFactor(value, factor, reason))
            {
                error = node.id + ": " + reason;
                return false;
            }

            if (factor > 1)
                element = std::make_unique<Oversampled>(std::move(element), factor);
        }

        element->prepare(*node.type, sampleRate, maxBlockSize);

        // Options after prepare: they can change what the element does, and
        // therefore how much latency it reports. An option the element
        // recognises but cannot apply fails the load, located at the node, so
        // the failure is visible rather than a circuit that runs wrong.
        for (const auto& [key, value] : node.options)
        {
            std::string optionError;
            if (! element->setOption(key, value, optionError))
            {
                error = node.id + ": val:" + key + " - " + optionError;
                return false;
            }
        }

        element->reset();
        latency += element->latencyInSamples();

        std::vector<float> values;
        values.reserve(node.controlValues.size());
        for (const double v : node.controlValues)
            values.push_back(static_cast<float>(v));

        maxAudioIn    = std::max(maxAudioIn,    node.audioInBuffers.size());
        maxAudioOut   = std::max(maxAudioOut,   node.audioOutBuffers.size());
        maxControlIn  = std::max(maxControlIn,  values.size());
        maxControlOut = std::max(maxControlOut, node.controlOutSlots.size());

        graph->elements.push_back(std::move(element));
        graph->controlValues.push_back(std::move(values));
    }

    // Carry forward any control overrides the host has already set.
    for (const auto& [nodeId, portSymbol, value] : pendingControls)
    {
        for (std::size_t i = 0; i < circuit.nodes.size(); ++i)
        {
            const auto& node = circuit.nodes[i];
            if (node.id != nodeId)
                continue;

            int index = 0;
            for (const auto& port : node.type->ports)
            {
                if (! port.input || ! port.control)
                    continue;

                if (port.symbol == portSymbol && index < static_cast<int>(graph->controlValues[i].size()))
                    graph->controlValues[i][static_cast<std::size_t>(index)] = value;
                ++index;
            }
        }
    }

    graph->audioInPtrs.assign(maxAudioIn, nullptr);
    graph->audioOutPtrs.assign(maxAudioOut, nullptr);
    graph->controlIn.assign(maxControlIn, 0.0f);
    graph->controlOut.assign(maxControlOut, 0.0f);

    // Waveform taps observe the audio output of monitor elements. Everything
    // is allocated here so process() only performs a bounded copy.
    graph->nodeTap.assign(circuit.nodes.size(), -1);
    for (std::size_t i = 0; i < circuit.nodes.size(); ++i)
    {
        const auto& node = circuit.nodes[i];
        const bool isTap = node.implementation == "Oscilloscope"
            || node.implementation == "FreqAnalyzer";
        if (! isTap || node.audioOutBuffers.empty())
            continue;

        auto tap = std::make_unique<Graph::Tap>();
        tap->nodeId      = node.id;
        tap->nodeIndex   = static_cast<int>(i);
        tap->bufferIndex = node.audioOutBuffers[0];
        tap->ring.assign(kTapRingSize, 0.0f);
        graph->nodeTap[i] = static_cast<int>(graph->taps.size());
        graph->taps.push_back(std::move(tap));
    }

    reportedLatency.store(latency, std::memory_order_relaxed);

    // The pool belongs to the circuit, so installing one starts every voice
    // idle rather than carrying the previous circuit's notes into it.
    numVoices = std::min(circuit.numVoices, kMaxVoices);
    for (auto& voice : voices)
        voice = Voice{};
    voiceClock = 0;

    Graph* installed = graph.release();
    Graph* previous  = active.exchange(installed, std::memory_order_acq_rel);
    retire(previous);

    return true;
}

void ValisEngine::retire(Graph* graph)
{
    if (graph == nullptr)
        return;

    graveyard.push_back({graph, blockCounter.load(std::memory_order_acquire)});
    collectGarbage();
}

void ValisEngine::collectGarbage()
{
    const auto now = blockCounter.load(std::memory_order_acquire);

    // A graph is safe to free once the audio thread has started a later block
    // than the one in flight when it was retired, since process() reads the
    // active pointer once at the top of each block.
    graveyard.erase(std::remove_if(graveyard.begin(), graveyard.end(),
                                   [&](const Retired& retired)
                                   {
                                       if (now > retired.atBlock + 1)
                                       {
                                           delete retired.graph;
                                           return true;
                                       }
                                       return false;
                                   }),
                    graveyard.end());
}

void ValisEngine::setControl(const std::string& nodeId,
                             const std::string& portSymbol,
                             float value)
{
    const auto it = std::find_if(pendingControls.begin(), pendingControls.end(),
                                 [&](const auto& entry)
                                 {
                                     return std::get<0>(entry) == nodeId
                                         && std::get<1>(entry) == portSymbol;
                                 });

    if (it != pendingControls.end())
        std::get<2>(*it) = value;
    else
        pendingControls.emplace_back(nodeId, portSymbol, value);

    // Applying to the live graph is a plain float store: the audio thread reads
    // these without synchronisation, and a torn read of one control is
    // acceptable where a lock would not be.
    if (Graph* graph = active.load(std::memory_order_acquire))
    {
        for (std::size_t i = 0; i < graph->circuit.nodes.size(); ++i)
        {
            const auto& node = graph->circuit.nodes[i];
            if (node.id != nodeId)
                continue;

            int index = 0;
            for (const auto& port : node.type->ports)
            {
                if (! port.input || ! port.control)
                    continue;

                if (port.symbol == portSymbol
                    && index < static_cast<int>(graph->controlValues[i].size()))
                    graph->controlValues[i][static_cast<std::size_t>(index)] = value;
                ++index;
            }
        }
    }
}

std::optional<float> ValisEngine::getControl(const std::string& nodeId,
                                             const std::string& portSymbol) const
{
    // The override list is the record of what has been set; the graph's own
    // values came from the circuit and are the model's business, not ours.
    const auto it = std::find_if(pendingControls.begin(), pendingControls.end(),
                                 [&](const auto& entry)
                                 {
                                     return std::get<0>(entry) == nodeId
                                         && std::get<1>(entry) == portSymbol;
                                 });

    if (it != pendingControls.end())
        return std::get<2>(*it);

    return std::nullopt;
}


std::optional<float> ValisEngine::getControlOutput(const std::string& nodeId,
                                                    const std::string& portSymbol) const
{
    const auto* graph = active.load(std::memory_order_acquire);
    if (!graph) return std::nullopt;

    for (const auto& node : graph->circuit.nodes)
    {
        if (node.id != nodeId || node.type == nullptr)
            continue;

        int idx = 0;
        for (const auto& port : node.type->ports)
        {
            if (!port.input && port.control)
            {
                if (port.symbol == portSymbol)
                {
                    if (idx < static_cast<int>(node.controlOutSlots.size()))
                    {
                        const int slot = node.controlOutSlots[static_cast<std::size_t>(idx)];
                        if (slot >= 0 && slot < static_cast<int>(graph->controlStore.size()))
                            return graph->controlStore[static_cast<std::size_t>(slot)];
                    }
                    return std::nullopt;
                }
                ++idx;
            }
        }
        break;
    }
    return std::nullopt;
}

std::vector<std::string> ValisEngine::tapNodes() const
{
    const auto* graph = active.load(std::memory_order_acquire);
    if (graph == nullptr)
        return {};

    std::vector<std::string> ids;
    for (const auto& tap : graph->taps)
        ids.push_back(tap->nodeId);
    return ids;
}

int ValisEngine::readTap(const std::string& nodeId, float* dest, int maxSamples) const
{
    if (dest == nullptr || maxSamples <= 0)
        return 0;

    const auto* graph = active.load(std::memory_order_acquire);
    if (graph == nullptr)
        return 0;

    for (const auto& tap : graph->taps)
    {
        if (tap->nodeId != nodeId)
            continue;

        const auto written = tap->written.load(std::memory_order_relaxed);
        const auto cap = static_cast<std::uint64_t>(kTapRingSize);
        const auto count = std::min({static_cast<std::uint64_t>(maxSamples), written, cap});
        const auto start = written - count;
        for (std::uint64_t k = 0; k < count; ++k)
            dest[k] = tap->ring[(start + k) % cap];
        return static_cast<int>(count);
    }
    return 0;
}

void ValisEngine::noteOn(int noteNumber, float velocity) noexcept
{
    const float vel = velocity > 0.0f ? velocity : 1.0f;

    allocateVoice(noteNumber, vel);
    ++heldNotes;
    lastNoteNumber = noteNumber;
    lastVelocity   = vel;
    gate = true;

    if (noteNumber >= 0 && noteNumber < 128)
    {
        const auto idx = static_cast<std::size_t>(noteNumber);
        activeNoteVelocities[idx] = vel;
        triggeredNoteVelocities[idx] = vel;
    }
}

void ValisEngine::noteOff(int noteNumber) noexcept
{
    releaseVoice(noteNumber);

    if (heldNotes > 0)
        --heldNotes;

    gate = heldNotes > 0;

    if (noteNumber >= 0 && noteNumber < 128)
    {
        activeNoteVelocities[static_cast<std::size_t>(noteNumber)] = 0.0f;
    }
}

/// Picks the voice a new note goes to. Deterministic in every branch: a free
/// voice first, then the one released longest ago, then the oldest still held.
/// Returns -1 when the circuit has no pool.
int ValisEngine::allocateVoice(int note, float velocity) noexcept
{
    if (numVoices <= 0)
        return -1;

    const auto start = [this, note, velocity](int v)
    {
        auto& voice = voices[static_cast<std::size_t>(v)];
        voice.started   = true;
        voice.gate      = true;
        voice.note      = note;
        voice.velocity  = velocity;
        voice.startedAt = ++voiceClock;
        return v;
    };

    int best = -1;

    // A voice that has never sounded is free with no tail to cut short.
    for (int v = 0; v < numVoices; ++v)
        if (! voices[static_cast<std::size_t>(v)].started)
            return start(v);

    // Otherwise the one whose note was let go longest ago.
    for (int v = 0; v < numVoices; ++v)
    {
        const auto& voice = voices[static_cast<std::size_t>(v)];
        if (voice.gate)
            continue;

        if (best < 0 || voice.startedAt < voices[static_cast<std::size_t>(best)].startedAt)
            best = v;
    }

    if (best >= 0)
        return start(best);

    // Every voice is still held, so the oldest one is taken from it.
    best = 0;
    for (int v = 1; v < numVoices; ++v)
        if (voices[static_cast<std::size_t>(v)].startedAt
            < voices[static_cast<std::size_t>(best)].startedAt)
            best = v;

    return start(best);
}

/// Closes the gate of the voice sounding `note`. The voice keeps its note so
/// whatever is in it can finish its release; allocation may still take it.
void ValisEngine::releaseVoice(int note) noexcept
{
    int oldest = -1;

    for (int v = 0; v < numVoices; ++v)
    {
        const auto& voice = voices[static_cast<std::size_t>(v)];
        if (! voice.gate || voice.note != note)
            continue;

        if (oldest < 0 || voice.startedAt < voices[static_cast<std::size_t>(oldest)].startedAt)
            oldest = v;
    }

    if (oldest >= 0)
        voices[static_cast<std::size_t>(oldest)].gate = false;
}

void ValisEngine::queueEvent(const NoteEvent& event) noexcept
{
    if (numEvents >= kMaxEventsPerBlock)
        return;

    events[static_cast<std::size_t>(numEvents++)] = event;
}

void ValisEngine::queueNoteOn(int noteNumber, float velocity, int sampleOffset) noexcept
{
    queueEvent({std::max(0, sampleOffset), NoteEvent::Kind::On, noteNumber, velocity});
}

void ValisEngine::queueNoteOff(int noteNumber, int sampleOffset) noexcept
{
    queueEvent({std::max(0, sampleOffset), NoteEvent::Kind::Off, noteNumber, 0.0f});
}

void ValisEngine::queueAllNotesOff(int sampleOffset) noexcept
{
    queueEvent({std::max(0, sampleOffset), NoteEvent::Kind::AllOff, 0, 0.0f});
}

void ValisEngine::applyEvent(const NoteEvent& event) noexcept
{
    switch (event.kind)
    {
        case NoteEvent::Kind::On:     noteOn(event.note, event.velocity); break;
        case NoteEvent::Kind::Off:    noteOff(event.note);                break;
        case NoteEvent::Kind::AllOff: allNotesOff();                      break;
    }
}

void ValisEngine::allNotesOff() noexcept
{
    for (auto& voice : voices)
        voice.gate = false;

    heldNotes = 0;
    gate = false;
    activeNoteVelocities.fill(0.0f);
    triggeredNoteVelocities.fill(0.0f);
}

void ValisEngine::process(const float* input, float* output, int numSamples) noexcept
{
    process(input, nullptr, output, nullptr, numSamples);
}

void ValisEngine::process(const float* input, float* outputL, float* outputR, int numSamples) noexcept
{
    process(input, nullptr, outputL, outputR, numSamples);
}

void ValisEngine::process(const float* inputL, const float* inputR,
                          float* outputL, float* outputR, int numSamples) noexcept
{
    blockCounter.fetch_add(1, std::memory_order_acq_rel);

    Graph* graph = active.load(std::memory_order_acquire);
    if (graph == nullptr)
    {
        if (outputL != nullptr)
            std::memset(outputL, 0, static_cast<std::size_t>(numSamples) * sizeof(float));
        if (outputR != nullptr)
            std::memset(outputR, 0, static_cast<std::size_t>(numSamples) * sizeof(float));
        return;
    }

    // The host reports one position per block. Carrying it forward a slice at a
    // time from the tempo keeps a musical phase continuous inside the block,
    // which is the same reason control values run on their own grid.
    TransportInfo sliceTransport = transport;
    const double ppqPerSample = transport.tempoBpm / (60.0 * sampleRate);

    // Events arrive in the host's order, except that a keyboard event queued
    // alongside them carries offset 0. An insertion sort over at most
    // kMaxEventsPerBlock entries puts them back in order without allocating.
    for (int i = 1; i < numEvents; ++i)
    {
        const auto event = events[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && events[static_cast<std::size_t>(j)].sampleOffset > event.sampleOffset)
        {
            events[static_cast<std::size_t>(j + 1)] = events[static_cast<std::size_t>(j)];
            --j;
        }
        events[static_cast<std::size_t>(j + 1)] = event;
    }

    numOutputEvents = 0;

    int cursor = 0;
    int done = 0;
    while (done < numSamples)
    {
        // Everything located at or before this sample has happened by now.
        while (cursor < numEvents
               && events[static_cast<std::size_t>(cursor)].sampleOffset <= done)
            applyEvent(events[static_cast<std::size_t>(cursor++)]);

        const int intoSlice = static_cast<int>(streamPosition % kControlBlock);
        int slice = std::min(numSamples - done, kControlBlock - intoSlice);

        // Stop the slice where the next event starts, so the note begins on the
        // sample it was sent on rather than at the next control boundary.
        if (cursor < numEvents)
            slice = std::min(slice,
                             events[static_cast<std::size_t>(cursor)].sampleOffset - done);

        processSlice(*graph,
                     sliceTransport,
                     inputL != nullptr ? inputL + done : nullptr,
                     inputR != nullptr ? inputR + done : nullptr,
                     outputL != nullptr ? outputL + done : nullptr,
                     outputR != nullptr ? outputR + done : nullptr,
                     slice,
                     done);

        if (transport.playing)
            sliceTransport.ppqPosition += ppqPerSample * static_cast<double>(slice);

        streamPosition += static_cast<std::uint64_t>(slice);
        done += slice;
    }

    // An event a host located past the end of the block still belongs to this
    // block: better late by a few samples than silently dropped.
    while (cursor < numEvents)
        applyEvent(events[static_cast<std::size_t>(cursor++)]);

    numEvents = 0;
    triggeredNoteVelocities.fill(0.0f);
}

void ValisEngine::processSlice(Graph& graph,
                               const TransportInfo& sliceTransport,
                               const float* inputL,
                               const float* inputR,
                               float* outputL,
                               float* outputR,
                               int numSamples,
                               int sliceOffset) noexcept
{
    const auto& circuit = graph.circuit;

    for (std::size_t k = 0; k < 128; ++k)
        graph.currentNoteVelocities[k] = std::max(activeNoteVelocities[k], triggeredNoteVelocities[k]);

    // Silence is shared, so it has to be silent every slice: an element that
    // wrote through an unconnected input would poison every other reader.
    std::memset(graph.buffer(circuit.silenceBuffer), 0,
                static_cast<std::size_t>(numSamples) * sizeof(float));

    // The host's audio lands in each val:Input's output buffers. "left" and
    // "right" carry the first two host channels; "out" carries them summed, so
    // a mono circuit needs to know nothing about how many arrived.
    const float* const hostL = inputL;
    const float* const hostR = inputR != nullptr ? inputR : inputL;

    for (const int nodeIndex : circuit.inputNodes)
    {
        const auto& node = circuit.nodes[static_cast<std::size_t>(nodeIndex)];
        if (node.type == nullptr)
            continue;

        int index = 0;
        for (const auto& port : node.type->ports)
        {
            if (port.input || port.control)
                continue;

            const int slot = index++;
            if (static_cast<std::size_t>(slot) >= node.audioOutBuffers.size())
                continue;

            float* destination = graph.buffer(node.audioOutBuffers[static_cast<std::size_t>(slot)]);

            const float* source = port.symbol == "left"  ? hostL
                                : port.symbol == "right" ? hostR
                                                         : nullptr;

            if (source != nullptr)
            {
                std::memcpy(destination, source,
                            static_cast<std::size_t>(numSamples) * sizeof(float));
            }
            else if (hostL != nullptr)
            {
                // "out": the channels summed. Averaged, so a mono source keeps
                // its level whether the host sent it once or twice.
                if (hostR != nullptr && hostR != hostL)
                    for (int i = 0; i < numSamples; ++i)
                        destination[i] = 0.5f * (hostL[i] + hostR[i]);
                else
                    std::memcpy(destination, hostL,
                                static_cast<std::size_t>(numSamples) * sizeof(float));
            }
            else
            {
                std::memset(destination, 0,
                            static_cast<std::size_t>(numSamples) * sizeof(float));
            }
        }
    }

    for (std::size_t i = 0; i < circuit.nodes.size(); ++i)
    {
        const auto& node = circuit.nodes[i];

        // Sum any fan-in into its scratch buffer first.
        for (const auto& job : node.sumJobs)
        {
            float* destination = graph.buffer(job.destination);
            std::memset(destination, 0, static_cast<std::size_t>(numSamples) * sizeof(float));

            for (const int source : job.sources)
            {
                const float* from = graph.buffer(source);
                for (int s = 0; s < numSamples; ++s)
                    destination[s] += from[s];
            }
        }

        // Control inputs: the node's own values, then any control arc.
        const auto& values = graph.controlValues[i];
        for (std::size_t c = 0; c < values.size(); ++c)
            graph.controlIn[c] = values[c];

        for (const auto& link : circuit.controlLinks)
        {
            if (link.destNode != static_cast<int>(i))
                continue;

            const auto slot = static_cast<std::size_t>(link.sourceSlot);
            if (slot < graph.controlStore.size()
                && link.destControl >= 0
                && link.destControl < static_cast<int>(values.size()))
                graph.controlIn[static_cast<std::size_t>(link.destControl)] =
                    graph.controlStore[slot] * static_cast<float>(link.depth);
        }

        for (std::size_t p = 0; p < node.audioInBuffers.size(); ++p)
            graph.audioInPtrs[p] = graph.buffer(node.audioInBuffers[p]);
        for (std::size_t p = 0; p < node.audioOutBuffers.size(); ++p)
            graph.audioOutPtrs[p] = graph.buffer(node.audioOutBuffers[p]);

        ProcessArgs args;
        // A node in a voice pool sees its own voice's note rather than the
        // circuit's. This is the whole of what makes the pool polyphonic: the
        // elements inside are the ordinary monophonic ones.
        if (node.voice >= 0 && node.voice < numVoices)
        {
            const auto& voice = voices[static_cast<std::size_t>(node.voice)];
            args.gate       = voice.gate;
            args.velocity   = voice.velocity;
            args.noteNumber = voice.note;
        }
        else
        {
            args.gate       = gate;
            args.velocity   = lastVelocity;
            args.noteNumber = lastNoteNumber;
        }

        args.noteVelocities = graph.currentNoteVelocities.data();
        args.transport      = sliceTransport;
        args.silence        = graph.buffer(circuit.silenceBuffer);
        args.audioIn        = graph.audioInPtrs.data();
        args.audioOut       = graph.audioOutPtrs.data();
        args.numAudioIn    = static_cast<int>(node.audioInBuffers.size());
        args.numAudioOut   = static_cast<int>(node.audioOutBuffers.size());
        args.numSamples    = numSamples;
        args.controlIn     = graph.controlIn.data();
        args.numControlIn  = static_cast<int>(values.size());
        args.controlOut    = graph.controlOut.data();
        args.numControlOut = static_cast<int>(node.controlOutSlots.size());

        // One sink shared by the whole circuit for the block. An element that
        // declares no event output simply never writes to it.
        args.eventsOut     = outputEvents.data();
        args.numEventsOut  = &numOutputEvents;
        args.eventCapacity = kMaxOutputEventsPerBlock;
        args.sliceOffset   = sliceOffset;

        graph.elements[i]->process(args);

        // Waveform tap: bounded copy of this slice into the tap's ring. No
        // allocation, no lock; the message thread reads `written` to find it.
        if (i < graph.nodeTap.size())
        {
            const int tapIndex = graph.nodeTap[i];
            if (tapIndex >= 0
                && static_cast<std::size_t>(tapIndex) < graph.taps.size())
            {
                auto& tap = *graph.taps[static_cast<std::size_t>(tapIndex)];
                const float* from = graph.buffer(tap.bufferIndex);
                const auto at = tap.written.load(std::memory_order_relaxed);
                const auto cap = static_cast<std::uint64_t>(kTapRingSize);
                for (int s = 0; s < numSamples; ++s)
                    tap.ring[(at + static_cast<std::uint64_t>(s)) % cap] = from[s];
                tap.written.store(at + static_cast<std::uint64_t>(numSamples),
                                  std::memory_order_relaxed);
            }
        }

        // Publish this node's control outputs for downstream arcs.
        for (std::size_t c = 0; c < node.controlOutSlots.size(); ++c)
            graph.controlStore[static_cast<std::size_t>(node.controlOutSlots[c])] =
                graph.controlOut[c];
    }

    // The output element reads whatever arrives at its audio inputs (in, left, right).
    const auto& outputNode = circuit.nodes[static_cast<std::size_t>(circuit.outputNode)];

    auto findAudioInputIndex = [&](const char* symbol)
    {
        if (outputNode.type == nullptr)
            return -1;

        int index = 0;
        for (const auto& port : outputNode.type->ports)
        {
            if (! port.input || port.control)
                continue;

            if (port.symbol == symbol)
                return index;
            ++index;
        }

        return -1;
    };

    const int inPortIdx    = findAudioInputIndex("in");
    const int leftPortIdx  = findAudioInputIndex("left");
    const int rightPortIdx = findAudioInputIndex("right");

    const float* monoBuf  = (inPortIdx >= 0 && static_cast<std::size_t>(inPortIdx) < outputNode.audioInBuffers.size())
                                ? graph.buffer(outputNode.audioInBuffers[static_cast<std::size_t>(inPortIdx)])
                                : nullptr;
    const float* leftBuf  = (leftPortIdx >= 0 && static_cast<std::size_t>(leftPortIdx) < outputNode.audioInBuffers.size())
                                ? graph.buffer(outputNode.audioInBuffers[static_cast<std::size_t>(leftPortIdx)])
                                : nullptr;
    const float* rightBuf = (rightPortIdx >= 0 && static_cast<std::size_t>(rightPortIdx) < outputNode.audioInBuffers.size())
                                ? graph.buffer(outputNode.audioInBuffers[static_cast<std::size_t>(rightPortIdx)])
                                : nullptr;

    const bool hasLeft  = leftBuf  != nullptr && leftBuf  != graph.buffer(circuit.silenceBuffer);
    const bool hasRight = rightBuf != nullptr && rightBuf != graph.buffer(circuit.silenceBuffer);
    const bool hasMono  = monoBuf  != nullptr && monoBuf  != graph.buffer(circuit.silenceBuffer);

    const float* srcL = hasLeft ? leftBuf : (hasMono ? monoBuf : graph.buffer(circuit.silenceBuffer));
    const float* srcR = hasRight ? rightBuf : (hasMono ? monoBuf : graph.buffer(circuit.silenceBuffer));

    if (outputL != nullptr)
        std::memcpy(outputL, srcL, static_cast<std::size_t>(numSamples) * sizeof(float));
    if (outputR != nullptr)
        std::memcpy(outputR, srcR, static_cast<std::size_t>(numSamples) * sizeof(float));
}

}  // namespace valis

// src/engine/ValisEngine.cpp

#include "valis/ValisEngine.h"

#include "valis/Ontology.h"

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

        element->prepare(*node.type, sampleRate, maxBlockSize);

        // Options after prepare: they can change what the element does, and
        // therefore how much latency it reports.
        for (const auto& [key, value] : node.options)
            element->setOption(key, value);

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

    reportedLatency.store(latency, std::memory_order_relaxed);

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


void ValisEngine::noteOn(int noteNumber, float velocity) noexcept
{
    ++heldNotes;
    lastNoteNumber = noteNumber;
    const float vel = velocity > 0.0f ? velocity : 1.0f;
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
    if (heldNotes > 0)
        --heldNotes;

    gate = heldNotes > 0;

    if (noteNumber >= 0 && noteNumber < 128)
    {
        activeNoteVelocities[static_cast<std::size_t>(noteNumber)] = 0.0f;
    }
}

void ValisEngine::allNotesOff() noexcept
{
    heldNotes = 0;
    gate = false;
    activeNoteVelocities.fill(0.0f);
    triggeredNoteVelocities.fill(0.0f);
}

void ValisEngine::process(const float* input, float* output, int numSamples) noexcept
{
    process(input, output, nullptr, numSamples);
}

void ValisEngine::process(const float* input, float* outputL, float* outputR, int numSamples) noexcept
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

    int done = 0;
    while (done < numSamples)
    {
        const int intoSlice = static_cast<int>(streamPosition % kControlBlock);
        const int slice = std::min(numSamples - done, kControlBlock - intoSlice);

        processSlice(*graph,
                     input != nullptr ? input + done : nullptr,
                     outputL != nullptr ? outputL + done : nullptr,
                     outputR != nullptr ? outputR + done : nullptr,
                     slice);

        streamPosition += static_cast<std::uint64_t>(slice);
        done += slice;
    }

    triggeredNoteVelocities.fill(0.0f);
}

void ValisEngine::processSlice(Graph& graph,
                               const float* input,
                               float* outputL,
                               float* outputR,
                               int numSamples) noexcept
{
    const auto& circuit = graph.circuit;

    for (std::size_t k = 0; k < 128; ++k)
        graph.currentNoteVelocities[k] = std::max(activeNoteVelocities[k], triggeredNoteVelocities[k]);

    // Silence is shared, so it has to be silent every slice: an element that
    // wrote through an unconnected input would poison every other reader.
    std::memset(graph.buffer(circuit.silenceBuffer), 0,
                static_cast<std::size_t>(numSamples) * sizeof(float));

    // The host's audio lands in each val:Input's output buffer.
    for (const int nodeIndex : circuit.inputNodes)
    {
        const auto& node = circuit.nodes[static_cast<std::size_t>(nodeIndex)];
        if (node.audioOutBuffers.empty())
            continue;

        float* destination = graph.buffer(node.audioOutBuffers[0]);
        if (input != nullptr)
            std::memcpy(destination, input, static_cast<std::size_t>(numSamples) * sizeof(float));
        else
            std::memset(destination, 0, static_cast<std::size_t>(numSamples) * sizeof(float));
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
        args.gate           = gate;
        args.velocity       = lastVelocity;
        args.noteNumber     = lastNoteNumber;
        args.noteVelocities = graph.currentNoteVelocities.data();
        args.audioIn        = graph.audioInPtrs.data();
        args.audioOut       = graph.audioOutPtrs.data();
        args.numAudioIn    = static_cast<int>(node.audioInBuffers.size());
        args.numAudioOut   = static_cast<int>(node.audioOutBuffers.size());
        args.numSamples    = numSamples;
        args.controlIn     = graph.controlIn.data();
        args.numControlIn  = static_cast<int>(values.size());
        args.controlOut    = graph.controlOut.data();
        args.numControlOut = static_cast<int>(node.controlOutSlots.size());

        graph.elements[i]->process(args);

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

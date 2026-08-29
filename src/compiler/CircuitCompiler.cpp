// src/compiler/CircuitCompiler.cpp

#include "valis/CircuitCompiler.h"

#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

namespace valis {

namespace {

bool isUnitDelay(const ElementInstance& e)
{
    return e.type != nullptr && e.type->implementation == "UnitDelay";
}

/// Depth-first search over the audio graph, ignoring arcs that leave a unit
/// delay. A cycle that survives that cut has no sample of latency in it and
/// cannot be evaluated, so it is rejected by name.
bool findCycle(const std::vector<std::vector<int>>& adjacency,
               int node,
               std::vector<int>& state,      // 0 unvisited, 1 on stack, 2 done
               std::vector<int>& stack,
               std::vector<int>& cycleOut)
{
    state[static_cast<std::size_t>(node)] = 1;
    stack.push_back(node);

    for (const int next : adjacency[static_cast<std::size_t>(node)])
    {
        if (state[static_cast<std::size_t>(next)] == 1)
        {
            const auto start = std::find(stack.begin(), stack.end(), next);
            cycleOut.assign(start, stack.end());
            return true;
        }
        if (state[static_cast<std::size_t>(next)] == 0
            && findCycle(adjacency, next, state, stack, cycleOut))
            return true;
    }

    stack.pop_back();
    state[static_cast<std::size_t>(node)] = 2;
    return false;
}

}  // namespace

bool CircuitCompiler::compile(const CircuitModel& model,
                              const Ontology& ontology,
                              CompiledCircuit& out,
                              std::vector<Diagnostic>& diagnostics)
{
    (void) ontology;
    out = {};

    const auto& elements = model.elements();
    const auto initialErrors = diagnostics.size();

    std::unordered_map<std::string, int> indexOf;
    for (std::size_t i = 0; i < elements.size(); ++i)
        indexOf[elements[i].id] = static_cast<int>(i);

    // -- exactly one output -------------------------------------------------
    std::vector<int> outputs;
    for (std::size_t i = 0; i < elements.size(); ++i)
        if (elements[i].type != nullptr && elements[i].type->implementation == "Output")
            outputs.push_back(static_cast<int>(i));

    if (outputs.empty())
        diagnostics.push_back({"circuit has no val:Output", model.id()});
    else if (outputs.size() > 1)
        diagnostics.push_back({"circuit has " + std::to_string(outputs.size()) +
                               " val:Output elements; it needs exactly one", model.id()});

    // -- arcs ---------------------------------------------------------------
    // Two adjacency graphs: `orderAdj` drives the topological sort (audio +
    // control arcs, so control sources precede their destinations); `cycleAdj`
    // drives cycle detection (audio arcs only, UnitDelay edges cut, matching
    // the "val:UnitDelay breaks a loop" rule).
    std::vector<std::vector<int>> cycleAdj(elements.size());
    std::vector<std::vector<int>> orderAdj(elements.size());
    std::set<std::string> seenArcs;
    std::map<std::string, int> audioFanIn;   // "node\0port" -> count

    std::vector<CompiledCircuit::Link> audioLinks, controlLinkArcs;

    for (const auto& arc : model.arcs())
    {
        const auto* fromElement = model.findElement(arc.fromNode);
        const auto* toElement   = model.findElement(arc.toNode);

        if (fromElement == nullptr || fromElement->type == nullptr)
        {
            diagnostics.push_back({"arc starts at " + vocab::shortName(arc.fromNode) +
                                   ", which is not an element of this circuit", arc.id});
            continue;
        }
        if (toElement == nullptr || toElement->type == nullptr)
        {
            diagnostics.push_back({"arc ends at " + vocab::shortName(arc.toNode) +
                                   ", which is not an element of this circuit", arc.id});
            continue;
        }

        const auto* fromPort = fromElement->type->findPort(arc.fromPort);
        const auto* toPort   = toElement->type->findPort(arc.toPort);

        if (fromPort == nullptr)
        {
            diagnostics.push_back({vocab::shortName(fromElement->typeIri) + " has no port '" +
                                   arc.fromPort + "'", arc.id});
            continue;
        }
        if (toPort == nullptr)
        {
            diagnostics.push_back({vocab::shortName(toElement->typeIri) + " has no port '" +
                                   arc.toPort + "'", arc.id});
            continue;
        }

        // Direction: an arc runs from an output to an input.
        if (fromPort->input)
        {
            diagnostics.push_back({"arc starts at '" + arc.fromPort +
                                   "', which is an input port", arc.id});
            continue;
        }
        if (! toPort->input)
        {
            diagnostics.push_back({"arc ends at '" + arc.toPort +
                                   "', which is an output port", arc.id});
            continue;
        }

        // Rate: audio and control do not mix.
        if (fromPort->control != toPort->control)
        {
            diagnostics.push_back({std::string("arc joins a ") +
                                   (fromPort->control ? "control" : "audio") +
                                   " output to a " + (toPort->control ? "control" : "audio") +
                                   " input", arc.id});
            continue;
        }

        const std::string key = arc.fromNode + '\0' + arc.fromPort + '\0' +
                                arc.toNode + '\0' + arc.toPort;
        if (! seenArcs.insert(key).second)
        {
            diagnostics.push_back({"duplicate arc: this connection already exists", arc.id});
            continue;
        }

        const int fromIndex = indexOf[arc.fromNode];
        const int toIndex   = indexOf[arc.toNode];

        CompiledCircuit::Link link{fromIndex, toIndex, arc.fromPort, arc.toPort, arc.depth};

        if (toPort->control)
        {
            controlLinkArcs.push_back(link);
            // Control source must run before its destination so the value is
            // ready when the destination reads its controlIn array.
            orderAdj[static_cast<std::size_t>(fromIndex)].push_back(toIndex);
        }
        else
        {
            audioLinks.push_back(link);
            ++audioFanIn[arc.toNode + '\0' + arc.toPort];

            // A unit delay reads the previous block, so it does not constrain
            // the order and its outgoing edge is cut before cycle detection.
            if (! isUnitDelay(*fromElement))
            {
                cycleAdj[static_cast<std::size_t>(fromIndex)].push_back(toIndex);
                orderAdj[static_cast<std::size_t>(fromIndex)].push_back(toIndex);
            }
        }
    }

    // -- fan-in -------------------------------------------------------------
    // Two signals arriving at one audio input is only meaningful where the
    // element sums, so require an explicit Mixer rather than summing silently.
    for (const auto& [key, count] : audioFanIn)
    {
        if (count < 2)
            continue;

        const auto separator = key.find('\0');
        const std::string node = key.substr(0, separator);
        const std::string port = key.substr(separator + 1);

        const auto* element = model.findElement(node);
        if (element != nullptr && element->type != nullptr
            && element->type->implementation != "Mixer")
        {
            diagnostics.push_back({std::to_string(count) + " arcs arrive at '" + port +
                                   "'; only val:Mixer sums its inputs", node});
        }
    }

    // -- cycles -------------------------------------------------------------
    std::vector<int> state(elements.size(), 0), stack, cycle;
    for (std::size_t i = 0; i < elements.size(); ++i)
    {
        if (state[i] != 0)
            continue;

        stack.clear();
        cycle.clear();
        if (findCycle(cycleAdj, static_cast<int>(i), state, stack, cycle))
        {
            std::string path;
            for (const int n : cycle)
                path += (path.empty() ? "" : " -> ") + vocab::shortName(elements[static_cast<std::size_t>(n)].id);

            diagnostics.push_back({"feedback loop with no val:UnitDelay to break it: " +
                                   path, model.id()});
            break;
        }
    }

    if (diagnostics.size() != initialErrors)
        return false;

    // -- topological order --------------------------------------------------
    // Uses orderAdj (audio + control arcs) so control sources are always
    // processed before their destinations — no one-block delay on pitch or
    // envelope modulation.
    std::vector<int> inDegree(elements.size(), 0);
    for (const auto& edges : orderAdj)
        for (const int target : edges)
            ++inDegree[static_cast<std::size_t>(target)];

    std::vector<int> ready, order;
    for (std::size_t i = 0; i < elements.size(); ++i)
        if (inDegree[i] == 0)
            ready.push_back(static_cast<int>(i));

    // Sort the frontier by id so the order is reproducible run to run, which
    // matters for the golden-output tests.
    const auto byId = [&](int a, int b) {
        return elements[static_cast<std::size_t>(a)].id > elements[static_cast<std::size_t>(b)].id;
    };
    std::sort(ready.begin(), ready.end(), byId);

    while (! ready.empty())
    {
        const int node = ready.back();
        ready.pop_back();
        order.push_back(node);

        for (const int next : orderAdj[static_cast<std::size_t>(node)])
            if (--inDegree[static_cast<std::size_t>(next)] == 0)
                ready.push_back(next);

        std::sort(ready.begin(), ready.end(), byId);
    }

    if (order.size() != elements.size())
    {
        diagnostics.push_back({"circuit could not be ordered; a cycle escaped detection",
                               model.id()});
        return false;
    }

    // -- lower --------------------------------------------------------------
    std::vector<int> positionOf(elements.size(), -1);
    for (std::size_t i = 0; i < order.size(); ++i)
        positionOf[static_cast<std::size_t>(order[i])] = static_cast<int>(i);

    out.nodes.reserve(order.size());
    for (const int index : order)
    {
        const auto& element = elements[static_cast<std::size_t>(index)];

        CompiledCircuit::Node node;
        node.id             = element.id;
        node.implementation = element.type->implementation;
        node.type           = element.type;

        for (const auto* port : element.type->portsMatching(true, true))
            node.controlValues.push_back(element.valueOf(port->symbol));

        // Sorted, so the compiled result is reproducible.
        for (const auto& [key, value] : element.options)
            node.options.emplace_back(key, value);
        std::sort(node.options.begin(), node.options.end());

        out.nodes.push_back(std::move(node));
    }

    const auto remap = [&](CompiledCircuit::Link link) {
        link.from = positionOf[static_cast<std::size_t>(link.from)];
        link.to   = positionOf[static_cast<std::size_t>(link.to)];
        return link;
    };

    for (const auto& link : audioLinks)
        out.audioLinks.push_back(remap(link));

    out.outputNode = outputs.empty() ? -1 : positionOf[static_cast<std::size_t>(outputs.front())];
    for (std::size_t i = 0; i < elements.size(); ++i)
        if (elements[i].type != nullptr && elements[i].type->implementation == "Input")
            out.inputNodes.push_back(positionOf[i]);

    // -- buffers ------------------------------------------------------------
    //
    // One buffer per audio output port, plus one scratch buffer per summed
    // input, plus a single shared block of silence that every unconnected
    // input reads. Allocation happens once, here, on the message thread.
    int nextBuffer = 0;
    out.silenceBuffer = nextBuffer++;

    int nextControlSlot = 0;

    for (auto& node : out.nodes)
    {
        for (std::size_t i = 0; i < node.type->portsMatching(false, false).size(); ++i)
            node.audioOutBuffers.push_back(nextBuffer++);

        for (std::size_t i = 0; i < node.type->portsMatching(false, true).size(); ++i)
            node.controlOutSlots.push_back(nextControlSlot++);

        node.audioInBuffers.assign(node.type->portsMatching(true, false).size(),
                                   out.silenceBuffer);
    }

    // Point each audio input at its source's output buffer. A second arc into
    // the same input turns it into a sum, which the compiler has already
    // restricted to val:Mixer.
    const auto portIndex = [](const ElementType& type, const std::string& symbol,
                              bool input, bool control) {
        int index = 0;
        for (const auto* port : type.portsMatching(input, control))
        {
            if (port->symbol == symbol)
                return index;
            ++index;
        }
        return -1;
    };

    std::map<std::pair<int, int>, std::vector<int>> arrivals;   // (node, inputPort) -> buffers
    for (const auto& link : out.audioLinks)
    {
        const auto& source = out.nodes[static_cast<std::size_t>(link.from)];
        const auto& dest   = out.nodes[static_cast<std::size_t>(link.to)];

        const int sourcePort = portIndex(*source.type, link.fromPort, false, false);
        const int destPort   = portIndex(*dest.type,   link.toPort,   true,  false);
        if (sourcePort < 0 || destPort < 0)
            continue;

        arrivals[{link.to, destPort}].push_back(
            source.audioOutBuffers[static_cast<std::size_t>(sourcePort)]);
    }

    for (const auto& [where, sources] : arrivals)
    {
        auto& node = out.nodes[static_cast<std::size_t>(where.first)];

        if (sources.size() == 1)
        {
            // Read the producer's buffer directly: no copy.
            node.audioInBuffers[static_cast<std::size_t>(where.second)] = sources.front();
        }
        else
        {
            CompiledCircuit::SumJob job;
            job.destination = nextBuffer++;
            job.sources     = sources;
            node.audioInBuffers[static_cast<std::size_t>(where.second)] = job.destination;
            node.sumJobs.push_back(std::move(job));
        }
    }

    // -- control links ------------------------------------------------------
    for (const auto& arc : controlLinkArcs)
    {
        const auto link = remap(arc);
        const auto& source = out.nodes[static_cast<std::size_t>(link.from)];
        const auto& dest   = out.nodes[static_cast<std::size_t>(link.to)];

        const int sourcePort = portIndex(*source.type, link.fromPort, false, true);
        const int destPort   = portIndex(*dest.type,   link.toPort,   true,  true);
        if (sourcePort < 0 || destPort < 0)
            continue;

        CompiledCircuit::ControlLink resolved;
        resolved.sourceSlot  = source.controlOutSlots[static_cast<std::size_t>(sourcePort)];
        resolved.destNode    = link.to;
        resolved.destControl = destPort;
        resolved.depth       = link.depth;
        resolved.fromPort    = link.fromPort;
        resolved.toPort      = link.toPort;
        resolved.from        = link.from;
        resolved.to          = link.to;
        out.controlLinks.push_back(std::move(resolved));
    }

    out.numBuffers      = nextBuffer;
    out.numControlSlots = nextControlSlot;

    return out.isValid();
}

}  // namespace valis

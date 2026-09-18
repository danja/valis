// src/model/CircuitModel.cpp

#include "valis/CircuitModel.h"

#include "valis/Ontology.h"
#include "valis/Subcircuit.h"
#include "valis/TurtleStore.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <map>
#include <tuple>
#include <utility>

namespace valis {

std::string Diagnostic::toString() const
{
    if (line > 0)
        return std::to_string(line) + ":" + std::to_string(col) + ": " + message;

    if (subject.empty())
        return message;

    return vocab::shortName(subject) + ": " + message;
}

double ElementInstance::valueOf(const std::string& portSymbol) const
{
    if (const auto it = properties.find(portSymbol); it != properties.end())
        return it->second;

    if (type != nullptr)
        if (const auto* port = type->findProperty(portSymbol))
            return port->defaultValue;

    return 0.0;
}

const ElementInstance* CircuitModel::findElement(const std::string& iri) const
{
    const auto it = std::find_if(elementList.begin(), elementList.end(),
                                 [&](const ElementInstance& e) { return e.id == iri; });
    return it != elementList.end() ? &*it : nullptr;
}

namespace {

/// Reads one end of an arc. Endpoints are blank nodes carrying val:node and
/// val:port, so a missing one is a structural error worth naming.
bool readEndpoint(const rdf::TurtleStore& store,
                  const rdf::Node& arc,
                  const std::string& property,
                  const std::string& arcId,
                  std::string& node,
                  std::string& port,
                  std::vector<Diagnostic>& diagnostics)
{
    auto endpoint = store.object(arc, property);
    if (! endpoint)
    {
        diagnostics.push_back({"arc has no " + vocab::shortName(property), arcId});
        return false;
    }

    auto nodeTerm = store.object(endpoint, vocab::val::node);
    auto portTerm = store.object(endpoint, vocab::val::port);

    if (! nodeTerm || ! nodeTerm.isUri())
    {
        diagnostics.push_back({vocab::shortName(property) + " endpoint has no val:node", arcId});
        return false;
    }
    if (! portTerm || portTerm.string().empty())
    {
        diagnostics.push_back({vocab::shortName(property) + " endpoint has no val:port", arcId});
        return false;
    }

    node = std::string(nodeTerm.string());
    port = std::string(portTerm.string());
    return true;
}

/// Reads one element node: its class, its label, and every val: property,
/// sorted into control-port values and options. Returns nothing when the node
/// carries no usable rdf:type.
std::optional<ElementInstance> readElement(const rdf::TurtleStore& store,
                                           const Ontology& ontology,
                                           const rdf::Node& node,
                                           std::vector<Diagnostic>& diagnostics)
{
    ElementInstance instance;
    instance.id = std::string(node.string());

    auto typeTerm = store.object(node, vocab::rdf::type);
    if (! typeTerm)
    {
        diagnostics.push_back({"element has no rdf:type", instance.id});
        return std::nullopt;
    }

    instance.typeIri = std::string(typeTerm.string());
    instance.type    = ontology.find(instance.typeIri);

    if (instance.type == nullptr)
    {
        diagnostics.push_back({"unknown or abstract element class " +
                               vocab::shortName(instance.typeIri), instance.id});
        return std::nullopt;
    }

    if (auto label = store.object(node, vocab::rdfs::label))
        instance.label = std::string(label.string());

    // Any val: property naming a control input sets that port's value.
    store.forEachProperty(node, [&](const rdf::Node& predicate, const rdf::Node& object)
    {
        const auto local = vocab::shortName(predicate.string());
        if (predicate.string().rfind(vocab::VAL, 0) != 0)
            return;

        if (instance.type->findProperty(local) == nullptr)
        {
            // Not a control port, so it configures the element rather than
            // driving it: val:antialiasing and the like.
            if (object.isUri() || object.isLiteral())
                instance.options[local] = std::string(object.string());
            return;
        }

        if (auto value = object.asDouble())
            instance.properties[local] = *value;
        else if (auto flag = object.asBool())
            instance.properties[local] = *flag ? 1.0 : 0.0;
        else
            diagnostics.push_back({"value of val:" + local + " is not numeric", instance.id});
    });

    return instance;
}

/// The val: properties an instance node sets on the subcircuit's own control
/// ports. Anything else on the node is not the subcircuit's business.
std::unordered_map<std::string, double> readOverrides(const rdf::TurtleStore& store,
                                                      const rdf::Node& node,
                                                      const SubcircuitDef& def,
                                                      std::vector<Diagnostic>& diagnostics)
{
    std::unordered_map<std::string, double> overrides;

    store.forEachProperty(node, [&](const rdf::Node& predicate, const rdf::Node& object)
    {
        if (predicate.string().rfind(vocab::VAL, 0) != 0)
            return;

        const auto local = vocab::shortName(predicate.string());
        const auto* port = def.findPort(local);
        if (port == nullptr || ! port->desc.input || ! port->desc.control)
            return;

        if (auto value = object.asDouble())
            overrides[local] = *value;
        else if (auto flag = object.asBool())
            overrides[local] = *flag ? 1.0 : 0.0;
        else
            diagnostics.push_back({"value of val:" + local + " is not numeric",
                                   std::string(node.string())});
    });

    return overrides;
}

/// Expands val:Subcircuit instances into plain elements and arcs.
///
/// Every inner element is renamed by prefixing it with the instance it belongs
/// to, so two instances of one definition never collide. Arc endpoints that
/// name an instance port are redirected to the inner port it stands for, which
/// is what `portMap` records.
class Expander
{
public:
    Expander(const rdf::TurtleStore& s,
             const Ontology& o,
             const SubcircuitLibrary& l,
             std::vector<ElementInstance>& e,
             std::vector<Arc>& a,
             std::vector<Diagnostic>& d)
        : store(s), ontology(o), library(l), elements(e), arcs(a), diagnostics(d) {}

    using Endpoint = std::pair<std::string, std::string>;

    /// Nesting is bounded so a definition that instantiates itself, directly or
    /// through a chain, is a located error rather than a stack overflow.
    static constexpr int kMaxDepth = 16;

    /// A circuit cannot ask for more voices than the engine keeps state for.
    static constexpr int kMaxVoices = 16;

    /// Expands one instance `voices` times and sums the copies.
    ///
    /// The survey's point is that synth voices, sampler voices and grains are
    /// one pattern: a fixed population, allocated and released deterministically,
    /// with nothing on the heap. Here the population is copies of a subcircuit,
    /// stamped out at load time; the engine gives each copy its own note and
    /// picks which one a note goes to.
    void expandVoices(const std::string& instanceIri, const SubcircuitDef& def,
                      const std::unordered_map<std::string, double>& overrides,
                      int voices)
    {
        // A control output has no single value once there are several copies of
        // the element behind it, so there is nothing honest to expose.
        for (const auto& port : def.ports)
        {
            if (! port.desc.input && port.desc.control)
            {
                diagnostics.push_back({"control output '" + port.desc.symbol +
                                       "' cannot be exposed by a polyphonic subcircuit: "
                                       "there is one value per voice", instanceIri});
                return;
            }
        }

        std::map<Endpoint, std::vector<Endpoint>> perVoice;

        for (int voice = 0; voice < voices; ++voice)
        {
            const auto voiceIri = instanceIri + "/" + std::to_string(voice);
            expand(voiceIri, def, overrides, 0, voice);

            // Take what this copy's ports resolved to and gather it, so the
            // instance's own ports can stand for all of the copies at once.
            for (const auto& port : def.ports)
            {
                const auto it = portMap.find({voiceIri, port.desc.symbol});
                if (it == portMap.end())
                    continue;

                auto& targets = perVoice[{instanceIri, port.desc.symbol}];
                targets.insert(targets.end(), it->second.begin(), it->second.end());
            }
        }

        // An input reaches every voice. An output is summed by a mixer the
        // expansion adds, so everything downstream sees one signal and the
        // compiler's rule that only val:Mixer sums its inputs still holds.
        for (const auto& port : def.ports)
        {
            const auto key = Endpoint{instanceIri, port.desc.symbol};
            const auto it  = perVoice.find(key);
            if (it == perVoice.end())
                continue;

            if (port.desc.input)
            {
                portMap[key] = it->second;
                continue;
            }

            const auto* mixerType = ontology.find(vocab::valTerm("Mixer"));
            if (mixerType == nullptr)
            {
                diagnostics.push_back({"val:Mixer is missing from the ontology, so a "
                                       "polyphonic subcircuit cannot be summed", instanceIri});
                return;
            }

            ElementInstance mixer;
            mixer.id           = instanceIri + "/sum/" + port.desc.symbol;
            mixer.typeIri      = mixerType->classIri;
            mixer.type         = mixerType;
            mixer.label        = "voices";
            mixer.instance     = instanceIri;
            mixer.instanceType = def.id;
            elements.push_back(std::move(mixer));

            const auto mixerId = instanceIri + "/sum/" + port.desc.symbol;
            int index = 0;
            for (const auto& source : it->second)
            {
                Arc arc;
                arc.id       = mixerId + "/" + std::to_string(index++);
                arc.fromNode = source.first;
                arc.fromPort = source.second;
                arc.toNode   = mixerId;
                arc.toPort   = "in";
                arcs.push_back(std::move(arc));
            }

            portMap[key] = {{ mixerId, "out" }};
        }
    }

    void expand(const std::string& instanceIri, const SubcircuitDef& def,
                const std::unordered_map<std::string, double>& overrides, int depth,
                int voice = -1)
    {
        if (depth > kMaxDepth)
        {
            diagnostics.push_back({"subcircuit nesting is deeper than " +
                                   std::to_string(kMaxDepth) +
                                   ", which usually means it instantiates itself", instanceIri});
            return;
        }

        const auto rename = [&instanceIri](const std::string& inner)
        {
            return instanceIri + "/" + vocab::shortName(inner);
        };

        for (const auto& innerIri : def.elementIris)
        {
            const auto innerNode = store.uri(innerIri);
            const auto renamed   = rename(innerIri);

            auto typeTerm = store.object(innerNode, vocab::rdf::type);
            const std::string typeIri = typeTerm ? std::string(typeTerm.string()) : std::string();

            if (const auto* nested = library.find(typeIri))
            {
                expand(renamed, *nested,
                       readOverrides(store, innerNode, *nested, diagnostics), depth + 1, voice);
                continue;
            }

            auto element = readElement(store, ontology, innerNode, diagnostics);
            if (! element)
                continue;

            element->id           = renamed;
            element->instance     = instanceIri;
            element->instanceType = def.id;
            element->voice        = voice;
            elements.push_back(std::move(*element));
        }

        for (const auto& arcIri : def.arcIris)
        {
            const auto arcNode = store.uri(arcIri);

            Arc arc;
            arc.id = rename(arcIri);

            if (! readEndpoint(store, arcNode, vocab::val::from, arc.id,
                               arc.fromNode, arc.fromPort, diagnostics))
                continue;
            if (! readEndpoint(store, arcNode, vocab::val::to, arc.id,
                               arc.toNode, arc.toPort, diagnostics))
                continue;

            if (auto depthTerm = store.object(arcNode, vocab::valTerm("depth")); depthTerm.asDouble())
                arc.depth = *depthTerm.asDouble();

            arc.fromNode = rename(arc.fromNode);
            arc.toNode   = rename(arc.toNode);
            arcs.push_back(std::move(arc));
        }

        for (const auto& port : def.ports)
            portMap[{instanceIri, port.desc.symbol}] = {{ rename(port.innerNode), port.innerPort }};

        // A declared default is part of the face the subcircuit presents, so it
        // replaces whatever the inner element set. The instance's own value
        // then sits on top of that, exactly as a turned knob does.
        for (const auto& port : def.ports)
        {
            if (! port.desc.input || ! port.desc.control)
                continue;

            std::optional<double> value;
            if (port.hasDefault)
                value = port.desc.defaultValue;
            if (const auto it = overrides.find(port.desc.symbol); it != overrides.end())
                value = it->second;

            if (value)
                pending.push_back({ rename(port.innerNode), port.innerPort, *value });
        }
    }

    /// Follows the port map until every branch lands on a port of a real
    /// element, so an instance port that exposes a nested instance port
    /// resolves in one step from the caller's point of view. A port of a
    /// polyphonic instance resolves to one endpoint per voice.
    std::vector<Endpoint> resolve(std::string node, std::string port) const
    {
        std::vector<Endpoint> frontier{{ std::move(node), std::move(port) }};

        for (int hop = 0; hop <= kMaxDepth; ++hop)
        {
            std::vector<Endpoint> next;
            bool moved = false;

            for (const auto& endpoint : frontier)
            {
                const auto it = portMap.find(endpoint);
                if (it == portMap.end())
                {
                    next.push_back(endpoint);
                    continue;
                }

                moved = true;
                for (const auto& target : it->second)
                    next.push_back(target);
            }

            frontier = std::move(next);
            if (! moved)
                break;
        }

        return frontier;
    }

    /// Values an exposed control port sets on the element behind it. Applied
    /// after expansion, once every element exists to receive them.
    struct PendingValue { std::string node, port; double value; };
    std::vector<PendingValue> pending;

    /// (node, port) -> every (node, port) it stands for. One entry for an
    /// ordinary subcircuit port; one per voice for an input of a polyphonic
    /// one, so an arc reaching it fans out to all of them.
    std::map<Endpoint, std::vector<Endpoint>> portMap;

private:
    const rdf::TurtleStore& store;
    const Ontology& ontology;
    const SubcircuitLibrary& library;
    std::vector<ElementInstance>& elements;
    std::vector<Arc>& arcs;
    std::vector<Diagnostic>& diagnostics;
};

}  // namespace

bool CircuitModel::build(const rdf::TurtleStore& store,
                         const Ontology& ontology,
                         std::vector<Diagnostic>& diagnostics)
{
    elementList.clear();
    arcList.clear();
    paramList.clear();
    circuitId.clear();

    const auto circuits = store.subjectsOfType(vocab::val::Circuit);
    if (circuits.empty())
    {
        diagnostics.push_back({"no val:Circuit found", {}});
        return false;
    }
    if (circuits.size() > 1)
    {
        diagnostics.push_back({"more than one val:Circuit; a file describes one circuit", {}});
        return false;
    }

    const auto& circuit = circuits.front();
    circuitId = std::string(circuit.string());

    SubcircuitLibrary library;
    library.load(store, ontology, diagnostics);

    Expander expander(store, ontology, library, elementList, arcList, diagnostics);

    // -- elements ----------------------------------------------------------
    for (const auto& elementNode : store.objects(circuit, vocab::val::element))
    {
        const std::string id(elementNode.string());

        auto typeTerm = store.object(elementNode, vocab::rdf::type);
        const std::string typeIri = typeTerm ? std::string(typeTerm.string()) : std::string();

        if (const auto* def = library.find(typeIri))
        {
            const auto overrides = readOverrides(store, elementNode, *def, diagnostics);

            // val:voices turns one instance into a bounded pool of them.
            int voices = 1;
            if (auto declared = store.object(elementNode, vocab::valTerm("voices")))
            {
                if (auto count = declared.asInt())
                    voices = static_cast<int>(*count);
                else
                    voices = 0;   // not a number, reported below

                if (voices < 1 || voices > Expander::kMaxVoices)
                {
                    diagnostics.push_back({"val:voices must be between 1 and " +
                                           std::to_string(Expander::kMaxVoices), id});
                    continue;
                }
            }

            if (voices > 1)
                expander.expandVoices(id, *def, overrides, voices);
            else
                expander.expand(id, *def, overrides, 0);

            continue;
        }

        if (auto instance = readElement(store, ontology, elementNode, diagnostics))
            elementList.push_back(std::move(*instance));
    }

    if (elementList.empty())
    {
        diagnostics.push_back({"circuit declares no usable elements", circuitId});
        return false;
    }

    // -- arcs --------------------------------------------------------------
    for (const auto& arcNode : store.objects(circuit, vocab::val::arc))
    {
        Arc arc;
        arc.id = std::string(arcNode.string());

        if (! readEndpoint(store, arcNode, vocab::val::from, arc.id,
                           arc.fromNode, arc.fromPort, diagnostics))
            continue;

        if (! readEndpoint(store, arcNode, vocab::val::to, arc.id,
                           arc.toNode, arc.toPort, diagnostics))
            continue;

        if (auto depth = store.object(arcNode, vocab::valTerm("depth")); depth.asDouble())
            arc.depth = *depth.asDouble();

        arcList.push_back(std::move(arc));
    }

    // Subcircuit expansion has renamed the inner elements and left every
    // instance port in the expander's map, so redirect each endpoint onto the
    // element that really carries it before anything reads the topology.
    // An endpoint on a polyphonic instance stands for one port per voice, so an
    // arc reaching it becomes one arc per voice.
    std::vector<Arc> resolved;
    resolved.reserve(arcList.size());

    for (const auto& arc : arcList)
    {
        const auto sources = expander.resolve(arc.fromNode, arc.fromPort);
        const auto targets = expander.resolve(arc.toNode,   arc.toPort);

        int copy = 0;
        for (const auto& source : sources)
            for (const auto& target : targets)
            {
                Arc one = arc;
                one.fromNode = source.first;
                one.fromPort = source.second;
                one.toNode   = target.first;
                one.toPort   = target.second;

                // One arc per voice needs one identity per voice, or the
                // "declared but not claimed" check below would see duplicates.
                if (sources.size() * targets.size() > 1)
                    one.id = arc.id + "/" + std::to_string(copy++);

                resolved.push_back(std::move(one));
            }
    }

    arcList = std::move(resolved);

    for (auto& arc : arcList)
    {
        const auto* fromElement = findElement(arc.fromNode);
        const auto* toElement   = findElement(arc.toNode);
        if (fromElement != nullptr && fromElement->type != nullptr
            && toElement != nullptr && toElement->type != nullptr)
        {
            const auto* fromPort = fromElement->type->findPort(arc.fromPort);
            const auto* toPort   = toElement->type->findPort(arc.toPort);
            if (fromPort != nullptr && toPort != nullptr)
                arc.control = fromPort->control && toPort->control;
        }
    }

    // A value an exposed control port carries belongs to the element behind it,
    // and is applied after expansion so every element exists to receive one.
    for (const auto& value : expander.pending)
    {
        for (const auto& [node, port] : expander.resolve(value.node, value.port))
        {
            const auto it = std::find_if(elementList.begin(), elementList.end(),
                                         [&](const ElementInstance& e) { return e.id == node; });
            if (it != elementList.end())
                it->properties[port] = value.value;
        }
    }

    // An arc declared but not claimed by the circuit is almost always a typo in
    // the circuit's val:arc list, so say so rather than silently ignoring it.
    // An arc inside a subcircuit is claimed by its definition, not by the
    // circuit, and appears in arcList under its renamed instance form.
    const auto definitions = library.all();

    for (const auto& declared : store.subjectsOfType(vocab::val::Arc))
    {
        const std::string id(declared.string());
        const bool claimed = std::any_of(arcList.begin(), arcList.end(),
                                         [&](const Arc& a) { return a.id == id; });
        if (claimed)
            continue;

        const bool ownedBySubcircuit = std::any_of(
            definitions.begin(), definitions.end(),
            [&](const SubcircuitDef* def)
            {
                return std::find(def->arcIris.begin(), def->arcIris.end(), id) != def->arcIris.end();
            });

        if (! ownedBySubcircuit)
            diagnostics.push_back({"arc is declared but not listed in the circuit's val:arc", id});
    }

    // -- parameter bindings -------------------------------------------------
    for (const auto& paramNode : store.subjectsOfType(vocab::val::Param))
    {
        ParamBinding binding;
        const std::string id(paramNode.string());

        auto slot = store.object(paramNode, vocab::val::slot);
        if (! slot.asInt())
        {
            diagnostics.push_back({"val:Param has no integer val:slot", id});
            continue;
        }
        binding.slot = static_cast<int>(*slot.asInt());

        auto target = store.object(paramNode, vocab::val::target);
        auto property = store.object(paramNode, vocab::val::property);
        if (! target || ! property)
        {
            diagnostics.push_back({"val:Param needs both val:target and val:property", id});
            continue;
        }

        // A binding may name a subcircuit instance and one of its exposed
        // ports, which stands for a port on an element inside it. On a
        // polyphonic instance that is one port per voice; the binding takes the
        // first, and the rest are kept in step by the same control arc.
        const auto endpoints = expander.resolve(std::string(target.string()),
                                                vocab::shortName(property.string()));
        if (endpoints.empty())
            continue;

        binding.targetNode     = endpoints.front().first;
        binding.propertySymbol = endpoints.front().second;
        binding.alsoTargets.assign(endpoints.begin() + 1, endpoints.end());

        if (auto name = store.object(paramNode, vocab::lv2::name))
            binding.name = std::string(name.string());
        if (auto symbol = store.object(paramNode, vocab::lv2::symbol))
            binding.symbol = std::string(symbol.string());
        if (auto mn = store.object(paramNode, vocab::lv2::minimum); mn.asDouble())
            binding.minimum = *mn.asDouble();
        if (auto mx = store.object(paramNode, vocab::lv2::maximum); mx.asDouble())
            binding.maximum = *mx.asDouble();
        if (auto sec = store.object(paramNode, vocab::val::section))
            binding.section = std::string(sec.string());

        paramList.push_back(std::move(binding));
    }

    // RDF has no order, so the store returns the bindings in whatever order it
    // holds them. Slot order is the author's own, and it is what keeps a
    // val:section together as one run of consecutive knobs in the Controls view.
    std::sort(paramList.begin(), paramList.end(),
              [](const ParamBinding& a, const ParamBinding& b) { return a.slot < b.slot; });

    return true;
}

}  // namespace valis

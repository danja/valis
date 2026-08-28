// src/model/CircuitModel.cpp

#include "valis/CircuitModel.h"

#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/Vocabulary.h"

#include <algorithm>

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

    // -- elements ----------------------------------------------------------
    for (const auto& elementNode : store.objects(circuit, vocab::val::element))
    {
        ElementInstance instance;
        instance.id = std::string(elementNode.string());

        auto typeTerm = store.object(elementNode, vocab::rdf::type);
        if (! typeTerm)
        {
            diagnostics.push_back({"element has no rdf:type", instance.id});
            continue;
        }

        instance.typeIri = std::string(typeTerm.string());
        instance.type    = ontology.find(instance.typeIri);

        if (instance.type == nullptr)
        {
            diagnostics.push_back({"unknown or abstract element class " +
                                   vocab::shortName(instance.typeIri), instance.id});
            continue;
        }

        if (auto label = store.object(elementNode, vocab::rdfs::label))
            instance.label = std::string(label.string());

        // Any val: property naming a control input sets that port's value.
        store.forEachProperty(elementNode, [&](const rdf::Node& predicate,
                                               const rdf::Node& object)
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

        elementList.push_back(std::move(instance));
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

    // An arc declared but not claimed by the circuit is almost always a typo in
    // the circuit's val:arc list, so say so rather than silently ignoring it.
    for (const auto& declared : store.subjectsOfType(vocab::val::Arc))
    {
        const std::string id(declared.string());
        const bool claimed = std::any_of(arcList.begin(), arcList.end(),
                                         [&](const Arc& a) { return a.id == id; });
        if (! claimed)
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

        binding.targetNode     = std::string(target.string());
        binding.propertySymbol = vocab::shortName(property.string());

        if (auto name = store.object(paramNode, vocab::lv2::name))
            binding.name = std::string(name.string());
        if (auto symbol = store.object(paramNode, vocab::lv2::symbol))
            binding.symbol = std::string(symbol.string());

        paramList.push_back(std::move(binding));
    }

    return true;
}

}  // namespace valis

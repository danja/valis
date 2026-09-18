// src/model/Subcircuit.cpp

#include "valis/Subcircuit.h"

#include "valis/TurtleStore.h"
#include "valis/Vocabulary.h"

#include <algorithm>

namespace valis {

const SubcircuitPort* SubcircuitDef::findPort(std::string_view symbol) const
{
    const auto it = std::find_if(ports.begin(), ports.end(),
                                 [&](const SubcircuitPort& p) { return p.desc.symbol == symbol; });
    return it != ports.end() ? &*it : nullptr;
}

bool SubcircuitLibrary::load(const rdf::TurtleStore& store,
                             const Ontology& ontology,
                             std::vector<Diagnostic>& diagnostics)
{
    defs.clear();

    for (const auto& node : store.subjectsOfType(vocab::val::Subcircuit))
    {
        SubcircuitDef def;
        def.id = std::string(node.string());
        def.type.classIri = def.id;

        if (auto label = store.object(node, vocab::rdfs::label))
        {
            def.label = std::string(label.string());
            def.type.label = def.label;
        }

        for (const auto& portNode : store.objects(node, vocab::lv2::port))
        {
            auto desc = ontology.readPort(store, portNode);
            if (! desc)
            {
                diagnostics.push_back({"subcircuit port needs lv2:symbol and a direction", def.id});
                continue;
            }

            // A subcircuit port is a name for an inner port. Without the
            // mapping the outside would have nothing to connect to.
            auto innerNode = store.object(portNode, vocab::val::node);
            auto innerPort = store.object(portNode, vocab::val::port);

            if (! innerNode || ! innerNode.isUri() || ! innerPort || innerPort.string().empty())
            {
                diagnostics.push_back({"subcircuit port " + desc->symbol +
                                       " has no val:node and val:port naming what it exposes", def.id});
                continue;
            }

            SubcircuitPort mapped;
            mapped.desc       = *desc;
            mapped.innerNode  = std::string(innerNode.string());
            mapped.innerPort  = std::string(innerPort.string());
            mapped.hasDefault = static_cast<bool>(store.object(portNode, vocab::lv2::defaultV));

            if (def.findPort(mapped.desc.symbol) != nullptr)
            {
                diagnostics.push_back({"subcircuit declares port " + mapped.desc.symbol +
                                       " more than once", def.id});
                continue;
            }

            def.type.ports.push_back(mapped.desc);
            def.ports.push_back(std::move(mapped));
        }

        for (const auto& element : store.objects(node, vocab::val::element))
            def.elementIris.push_back(std::string(element.string()));

        for (const auto& arc : store.objects(node, vocab::val::arc))
            def.arcIris.push_back(std::string(arc.string()));

        if (def.elementIris.empty())
        {
            diagnostics.push_back({"subcircuit declares no val:element", def.id});
            continue;
        }
        if (def.ports.empty())
        {
            diagnostics.push_back({"subcircuit declares no lv2:port, so nothing can reach it", def.id});
            continue;
        }

        // Every exposed port must name an element the subcircuit actually owns,
        // or the expansion would rewrite an arc onto a node that is not there.
        bool ok = true;
        for (const auto& port : def.ports)
        {
            const bool owned = std::find(def.elementIris.begin(), def.elementIris.end(),
                                         port.innerNode) != def.elementIris.end();
            if (! owned)
            {
                diagnostics.push_back({"subcircuit port " + port.desc.symbol + " exposes " +
                                       vocab::shortName(port.innerNode) +
                                       ", which is not one of its val:element", def.id});
                ok = false;
            }
        }
        if (! ok)
            continue;

        if (defs.count(def.id) != 0)
        {
            diagnostics.push_back({"subcircuit is declared more than once", def.id});
            continue;
        }

        defs.emplace(def.id, std::move(def));
    }

    return true;
}

const SubcircuitDef* SubcircuitLibrary::find(const std::string& iri) const
{
    const auto it = defs.find(iri);
    return it != defs.end() ? &it->second : nullptr;
}

std::vector<const SubcircuitDef*> SubcircuitLibrary::all() const
{
    std::vector<const SubcircuitDef*> result;
    result.reserve(defs.size());
    for (const auto& [iri, def] : defs)
        result.push_back(&def);

    std::sort(result.begin(), result.end(),
              [](const SubcircuitDef* a, const SubcircuitDef* b) { return a->id < b->id; });
    return result;
}

}  // namespace valis

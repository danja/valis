// src/ops/CircuitDiff.cpp

#include "valis/CircuitDiff.h"

#include "valis/CircuitModel.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace valis {

namespace {

std::string name(const std::string& iri) { return vocab::shortName(iri); }

std::string number(double v)
{
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.6g", v);
    return buffer;
}

std::string arcKey(const Arc& a)
{
    return a.fromNode + '\n' + a.fromPort + '\n' + a.toNode + '\n' + a.toPort;
}

std::string arcText(const Arc& a)
{
    return name(a.fromNode) + "." + a.fromPort + " to " + name(a.toNode) + "." + a.toPort;
}

}  // namespace

std::vector<std::string> diffCircuits(const CircuitModel& before, const CircuitModel& after)
{
    std::vector<std::string> changes;

    // -- elements ---------------------------------------------------------------------
    std::map<std::string, const ElementInstance*> was, now;
    for (const auto& e : before.elements()) was[e.id] = &e;
    for (const auto& e : after.elements())  now[e.id] = &e;

    for (const auto& [id, element] : now)
        if (was.count(id) == 0)
            changes.push_back("added " + name(id) + " (val:" + name(element->typeIri) + ")");

    for (const auto& [id, element] : was)
        if (now.count(id) == 0)
            changes.push_back("removed " + name(id) + " (val:" + name(element->typeIri) + ")");

    for (const auto& [id, old] : was)
    {
        const auto found = now.find(id);
        if (found == now.end())
            continue;
        const auto& updated = *found->second;

        if (old->typeIri != updated.typeIri)
            changes.push_back(name(id) + " changed from val:" + name(old->typeIri) + " to val:" + name(updated.typeIri));

        // Values of control ports. A port the circuit does not mention has the ontology's default.
        std::set<std::string> ports;
        for (const auto& [port, value] : old->properties) ports.insert(port);
        for (const auto& [port, value] : updated.properties) ports.insert(port);
        for (const auto& port : ports)
        {
            const auto a = old->properties.find(port);
            const auto b = updated.properties.find(port);
            if (a != old->properties.end() && b != updated.properties.end())
            {
                if (a->second != b->second)
                    changes.push_back(name(id) + "." + port + " changed from " + number(a->second) + " to " + number(b->second));
            }
            else if (b != updated.properties.end())
                changes.push_back(name(id) + "." + port + " set to " + number(b->second));
            else
                changes.push_back(name(id) + "." + port + " no longer set (was " + number(a->second) + ")");
        }

        std::set<std::string> options;
        for (const auto& [key, value] : old->options) options.insert(key);
        for (const auto& [key, value] : updated.options) options.insert(key);
        for (const auto& key : options)
        {
            const auto a = old->options.find(key);
            const auto b = updated.options.find(key);
            if (a != old->options.end() && b != updated.options.end())
            {
                if (a->second != b->second)
                    changes.push_back(name(id) + " option " + key + " changed from '" + a->second + "' to '" + b->second + "'");
            }
            else if (b != updated.options.end())
                changes.push_back(name(id) + " option " + key + " set to '" + b->second + "'");
            else
                changes.push_back(name(id) + " option " + key + " removed (was '" + a->second + "')");
        }
    }

    // -- arcs -------------------------------------------------------------------------
    std::map<std::string, const Arc*> arcsBefore, arcsAfter;
    for (const auto& a : before.arcs()) arcsBefore[arcKey(a)] = &a;
    for (const auto& a : after.arcs())  arcsAfter[arcKey(a)] = &a;

    for (const auto& [key, arc] : arcsAfter)
        if (arcsBefore.count(key) == 0)
            changes.push_back("connected " + arcText(*arc) + (arc->control ? " (control)" : ""));

    for (const auto& [key, arc] : arcsBefore)
        if (arcsAfter.count(key) == 0)
            changes.push_back("disconnected " + arcText(*arc));

    for (const auto& [key, arc] : arcsAfter)
    {
        const auto found = arcsBefore.find(key);
        if (found != arcsBefore.end() && found->second->depth != arc->depth)
            changes.push_back("depth of " + arcText(*arc) + " changed from " + number(found->second->depth) + " to " + number(arc->depth));
    }

    // -- parameters ---------------------------------------------------------------------
    std::map<int, const ParamBinding*> paramsBefore, paramsAfter;
    for (const auto& p : before.params()) paramsBefore[p.slot] = &p;
    for (const auto& p : after.params())  paramsAfter[p.slot] = &p;

    const auto paramText = [](const ParamBinding& p)
    {
        return "slot " + std::to_string(p.slot) + " (" + p.name + ", " + name(p.targetNode) + "." + p.propertySymbol + ")";
    };

    for (const auto& [slot, p] : paramsAfter)
        if (paramsBefore.count(slot) == 0)
            changes.push_back("bound " + paramText(*p));

    for (const auto& [slot, p] : paramsBefore)
        if (paramsAfter.count(slot) == 0)
            changes.push_back("unbound " + paramText(*p));

    for (const auto& [slot, p] : paramsAfter)
    {
        const auto found = paramsBefore.find(slot);
        if (found == paramsBefore.end())
            continue;
        const auto& old = *found->second;
        if (old.targetNode != p->targetNode || old.propertySymbol != p->propertySymbol || old.name != p->name
            || old.minimum != p->minimum || old.maximum != p->maximum)
            changes.push_back(paramText(old) + " is now " + paramText(*p));
    }

    return changes;
}

}  // namespace valis

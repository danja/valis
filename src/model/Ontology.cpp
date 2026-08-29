// src/model/Ontology.cpp

#include "valis/Ontology.h"

#include "valis/TurtleStore.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <unordered_map>

namespace valis {

namespace {

/// units:unit may be a named unit (units:hz) or an inline blank node carrying
/// units:symbol. Both appear in the ontology, so handle both.
std::string readUnitSymbol(const rdf::TurtleStore& store,
                           const rdf::Node& port,
                           const std::unordered_map<std::string, std::string>& known)
{
    auto unit = store.object(port, vocab::units::unit);
    if (! unit)
        return {};

    // An inline unit carries its own symbol.
    if (auto symbol = store.object(unit, vocab::units::symbol))
        return std::string(symbol.string());

    // A named unit such as units:hz carries it in the LV2 units vocabulary.
    if (unit.isUri())
    {
        const std::string iri(unit.string());
        if (const auto it = known.find(iri); it != known.end())
            return it->second;

        // Not loaded: the local name is a readable fallback.
        return vocab::shortName(iri);
    }

    return {};
}

std::optional<PortDesc> readPort(const rdf::TurtleStore& store, const rdf::Node& port,
                                 const std::unordered_map<std::string, std::string>& units)
{
    PortDesc desc;

    auto symbol = store.object(port, vocab::lv2::symbol);
    if (! symbol || symbol.string().empty())
        return std::nullopt;

    desc.symbol = std::string(symbol.string());

    if (auto name = store.object(port, vocab::lv2::name))
        desc.name = std::string(name.string());
    else
        desc.name = desc.symbol;

    const bool isInput   = store.contains(port, vocab::rdf::type, store.uri(vocab::lv2::InputPort));
    const bool isOutput  = store.contains(port, vocab::rdf::type, store.uri(vocab::lv2::OutputPort));
    const bool isControl = store.contains(port, vocab::rdf::type, store.uri(vocab::lv2::ControlPort));

    if (! isInput && ! isOutput)
        return std::nullopt;

    desc.input   = isInput;
    desc.control = isControl;

    if (auto v = store.object(port, vocab::lv2::defaultV); v.asDouble())
        desc.defaultValue = *v.asDouble();
    if (auto v = store.object(port, vocab::lv2::minimum); v.asDouble())
        desc.minimum = *v.asDouble();
    if (auto v = store.object(port, vocab::lv2::maximum); v.asDouble())
        desc.maximum = *v.asDouble();

    desc.unitSymbol = readUnitSymbol(store, port, units);

    for (const auto& prop : store.objects(port, vocab::lv2::portProperty))
    {
        const auto iri = std::string(prop.string());
        if (iri == vocab::lv2::logarithmic) desc.logarithmic = true;
        if (iri == vocab::lv2::enumeration) desc.enumeration = true;
    }

    for (const auto& sp : store.objects(port, vocab::lv2::scalePoint))
    {
        auto label = store.object(sp, vocab::rdfs::label);
        auto val   = store.object(sp, vocab::rdf::value);
        if (label && val)
            if (auto v = val.asDouble())
                desc.scalePoints.push_back({ *v, std::string(label.string()) });
    }
    std::sort(desc.scalePoints.begin(), desc.scalePoints.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    return desc;
}

}  // namespace

// ---------------------------------------------------------------------------
// ElementType
// ---------------------------------------------------------------------------

const PortDesc* ElementType::findPort(std::string_view symbol) const
{
    const auto it = std::find_if(ports.begin(), ports.end(),
                                 [&](const PortDesc& p) { return p.symbol == symbol; });
    return it != ports.end() ? &*it : nullptr;
}

std::vector<const PortDesc*> ElementType::portsMatching(bool input, bool control) const
{
    std::vector<const PortDesc*> result;
    for (const auto& p : ports)
        if (p.input == input && p.control == control)
            result.push_back(&p);

    return result;
}

const PortDesc* ElementType::findProperty(std::string_view localName) const
{
    const auto* port = findPort(localName);
    return (port != nullptr && port->input && port->control) ? port : nullptr;
}

// ---------------------------------------------------------------------------
// Ontology
// ---------------------------------------------------------------------------

bool Ontology::loadFile(const std::string& path, std::vector<std::string>& errors)
{
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;

    if (! store.parseFile(path, parseErrors))
    {
        for (const auto& e : parseErrors)
            errors.push_back(path + ":" + e.toString());
        return false;
    }

    return loadFromStore(store, errors);
}

bool Ontology::loadUnits(const std::string& path, std::vector<std::string>& errors)
{
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;

    if (! store.parseFile(path, parseErrors))
    {
        for (const auto& e : parseErrors)
            errors.push_back(path + ":" + e.toString());
        return false;
    }

    for (const auto& unit : store.subjectsOfType(std::string(vocab::UNITS) + "Unit"))
        if (auto symbol = store.object(unit, vocab::units::symbol))
            unitSymbols[std::string(unit.string())] = std::string(symbol.string());

    return true;
}

bool Ontology::loadTurtle(std::string_view turtle, std::vector<std::string>& errors)
{
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;

    if (! store.parse(turtle, "urn:valis:ontology", parseErrors))
    {
        for (const auto& e : parseErrors)
            errors.push_back(e.toString());
        return false;
    }

    return loadFromStore(store, errors);
}

bool Ontology::loadFromStore(const rdf::TurtleStore& store, std::vector<std::string>& errors)
{
    // Aliases first: val:NonLinear owl:equivalentClass val:Transfer.
    for (const auto& alias : store.subjects(vocab::owl::equivalentClass,
                                            store.uri(vocab::val::Transfer)))
        aliases[std::string(alias.string())] = vocab::val::Transfer;

    // A class is instantiable exactly when it carries val:implementation.
    const auto implProperty = vocab::val::implementation;
    for (const auto& type : store.subjectsOfType(vocab::rdfs::Class))
    {
        auto impl = store.object(type, implProperty);
        if (! impl)
            continue;   // abstract: declared for the taxonomy, never constructed

        ElementType element;
        element.classIri       = std::string(type.string());
        element.implementation = std::string(impl.string());

        if (auto label = store.object(type, vocab::rdfs::label))
            element.label = std::string(label.string());

        if (auto linear = store.object(type, vocab::val::linear); linear.asBool())
            element.linear = *linear.asBool();

        if (auto aa = store.object(type, vocab::valTerm("antialiasing")))
            element.antialiasing = std::string(aa.string());

        for (const auto& port : store.objects(type, vocab::lv2::port))
        {
            if (auto desc = readPort(store, port, unitSymbols))
                element.ports.push_back(std::move(*desc));
            else
                errors.push_back(element.classIri + ": port missing lv2:symbol or direction");
        }

        if (element.ports.empty())
            errors.push_back(element.classIri + ": declares val:implementation but no lv2:port");

        // Two ports with the same symbol would make an arc endpoint ambiguous.
        std::vector<std::string> symbols;
        for (const auto& p : element.ports)
            symbols.push_back(p.symbol);
        std::sort(symbols.begin(), symbols.end());
        if (std::adjacent_find(symbols.begin(), symbols.end()) != symbols.end())
            errors.push_back(element.classIri + ": duplicate lv2:symbol among its ports");

        typesByIri[element.classIri] = std::move(element);
    }

    if (typesByIri.empty())
        errors.push_back("ontology declares no implementable classes");

    return errors.empty();
}

const ElementType* Ontology::find(std::string_view classIri) const
{
    std::string iri(classIri);

    if (const auto alias = aliases.find(iri); alias != aliases.end())
        iri = alias->second;

    const auto it = typesByIri.find(iri);
    return it != typesByIri.end() ? &it->second : nullptr;
}

std::vector<const ElementType*> Ontology::types() const
{
    std::vector<const ElementType*> result;
    result.reserve(typesByIri.size());
    for (const auto& [iri, type] : typesByIri)
        result.push_back(&type);

    std::sort(result.begin(), result.end(),
              [](const ElementType* a, const ElementType* b) { return a->classIri < b->classIri; });
    return result;
}

std::vector<std::string> Ontology::implementationKeys() const
{
    std::vector<std::string> keys;
    keys.reserve(typesByIri.size());
    for (const auto& [iri, type] : typesByIri)
        keys.push_back(type.implementation);

    std::sort(keys.begin(), keys.end());
    return keys;
}

}  // namespace valis

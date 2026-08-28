// src/ops/OpDispatcher.cpp

#include "valis/Ops.h"

#include "valis/CircuitCompiler.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"
#include "valis/Vocabulary.h"

#include <cstdio>

#include <algorithm>
#include <functional>
#include <sstream>

namespace valis {

namespace {

/// Minimal JSON emission. The MCP layer needs structured output and juce::var
/// is not available to valis_core's dependents at this level, so build it here
/// and keep the escaping in one place.
std::string escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                }
                else
                {
                    out += c;
                }
        }
    }
    return out;
}

std::string quoted(std::string_view text) { return "\"" + escape(text) + "\""; }

std::string number(double value)
{
    std::ostringstream out;
    out.precision(10);
    out << value;
    return out.str();
}

}  // namespace

// ---------------------------------------------------------------------------
// Circuit source
// ---------------------------------------------------------------------------

OpResult OpDispatcher::getTurtle() const
{
    if (! ctx.readTurtle)
        return OpResult::failure("no circuit source is attached");

    return OpResult::success(ctx.readTurtle());
}

OpResult OpDispatcher::setTurtle(const std::string& turtle)
{
    if (! ctx.writeTurtle)
        return OpResult::failure("this circuit is read only");

    OpResult result;
    result.ok = ctx.writeTurtle(turtle, result.diagnostics);
    return result;
}

OpResult OpDispatcher::validate(const std::string& turtle) const
{
    if (ctx.ontology == nullptr)
        return OpResult::failure("no ontology loaded");

    OpResult result;

    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    if (! store.parse(turtle, "urn:valis:circuit", parseErrors))
    {
        for (const auto& e : parseErrors)
            result.diagnostics.push_back({e.message, {}, e.line, e.col});
        return result;
    }

    CircuitModel model;
    if (! model.build(store, *ctx.ontology, result.diagnostics))
        return result;

    CompiledCircuit compiled;
    CircuitCompiler compiler;
    result.ok = compiler.compile(model, *ctx.ontology, compiled, result.diagnostics);

    if (result.ok)
        result.value = std::to_string(compiled.nodes.size()) + " nodes";

    return result;
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

std::vector<ElementTypeInfo> OpDispatcher::listElementTypes() const
{
    std::vector<ElementTypeInfo> result;
    if (ctx.ontology == nullptr)
        return result;

    for (const auto* type : ctx.ontology->types())
    {
        ElementTypeInfo info;
        info.classIri       = type->classIri;
        info.label          = type->label;
        info.implementation = type->implementation;
        info.linear         = type->linear;

        for (const auto& port : type->ports)
            info.ports.push_back({port.symbol, port.name, port.unitSymbol,
                                  port.input, port.control,
                                  port.defaultValue, port.minimum, port.maximum});

        result.push_back(std::move(info));
    }
    return result;
}

OpResult OpDispatcher::getGraph() const
{
    if (! ctx.readModel)
        return OpResult::failure("no circuit is loaded");

    const auto* model = ctx.readModel();
    if (model == nullptr)
        return OpResult::failure("no circuit is loaded");

    std::string json = "{\"id\":" + quoted(model->id()) + ",\"elements\":[";

    bool first = true;
    for (const auto& element : model->elements())
    {
        if (! first) json += ",";
        first = false;

        json += "{\"id\":" + quoted(element.id)
              + ",\"type\":" + quoted(element.typeIri)
              + ",\"label\":" + quoted(element.label)
              + ",\"properties\":{";

        bool firstProperty = true;
        for (const auto& [name, value] : element.properties)
        {
            if (! firstProperty) json += ",";
            firstProperty = false;
            json += quoted(name) + ":" + number(value);
        }
        json += "}}";
    }

    json += "],\"arcs\":[";
    first = true;
    for (const auto& arc : model->arcs())
    {
        if (! first) json += ",";
        first = false;

        json += "{\"id\":" + quoted(arc.id)
              + ",\"from\":{\"node\":" + quoted(arc.fromNode) + ",\"port\":" + quoted(arc.fromPort) + "}"
              + ",\"to\":{\"node\":"   + quoted(arc.toNode)   + ",\"port\":" + quoted(arc.toPort)   + "}"
              + ",\"depth\":" + number(arc.depth) + "}";
    }

    json += "]}";
    return OpResult::success(std::move(json));
}

// ---------------------------------------------------------------------------
// Graph editing
//
// Every edit is expressed as a change to the Turtle source, then re-validated
// through setTurtle. There is no second path into the model, so an edit made
// over MCP goes through exactly what the text editor goes through.
// ---------------------------------------------------------------------------

namespace {

/// Applies a structural edit, restoring the previous source if it will not
/// validate. Ops are atomic: an edit either happens or leaves nothing behind.
OpResult applyEdit(OpDispatcher& ops,
                   const std::function<std::string()>& readTurtle,
                   std::string edited)
{
    const auto previous = readTurtle();

    auto result = ops.setTurtle(std::move(edited));
    if (! result.ok)
    {
        std::vector<Diagnostic> ignored;
        ops.setTurtle(previous);
    }
    return result;
}

/// Appends a triple block before the end of the source. Structural edits
/// re-serialise, so hand formatting is not preserved - the text view stays
/// authoritative for prose.
std::string withCircuitMember(const std::string& turtle,
                              const std::string& circuitId,
                              const std::string& property,
                              const std::string& member)
{
    (void) circuitId;
    return turtle + "\n<" + circuitId + "> <" + property + "> <" + member + "> .\n";
}

}  // namespace

OpResult OpDispatcher::addNode(const std::string& id, const std::string& classIri)
{
    if (ctx.ontology == nullptr)
        return OpResult::failure("no ontology loaded");

    const auto resolved = vocab::expand(classIri);
    if (ctx.ontology->find(resolved) == nullptr)
        return OpResult::failure("unknown or abstract element class " + classIri);

    const auto* model = ctx.readModel ? ctx.readModel() : nullptr;
    if (model == nullptr)
        return OpResult::failure("no circuit is loaded");

    if (model->findElement(id) != nullptr)
        return OpResult::failure("an element with id " + id + " already exists");

    auto turtle = ctx.readTurtle();
    turtle += "\n<" + id + "> <" + vocab::rdf::type + "> <" + resolved + "> .\n";
    turtle = withCircuitMember(turtle, model->id(), vocab::val::element, id);

    return applyEdit(*this, ctx.readTurtle, std::move(turtle));
}

OpResult OpDispatcher::removeNode(const std::string& id)
{
    const auto* model = ctx.readModel ? ctx.readModel() : nullptr;
    if (model == nullptr)
        return OpResult::failure("no circuit is loaded");

    const auto* element = model->findElement(id);
    if (element == nullptr)
        return OpResult::failure("no element with id " + id);

    // Removing a node removes the arcs that touch it, or the result would not
    // validate. Rebuild the source from the model rather than editing text.
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    if (! store.parse(ctx.readTurtle(), "urn:valis:circuit", parseErrors))
        return OpResult::failure("current circuit no longer parses");

    auto subject = store.uri(id);
    std::vector<std::pair<rdf::Node, rdf::Node>> toRemove;
    store.forEachProperty(subject, [&](const rdf::Node& p, const rdf::Node& o)
                          { toRemove.emplace_back(p, o); });

    for (const auto& [predicate, object] : toRemove)
        store.remove(subject, predicate, object);

    store.remove(store.uri(model->id()), store.uri(vocab::val::element), subject);

    for (const auto& arc : model->arcs())
    {
        if (arc.fromNode != id && arc.toNode != id)
            continue;

        auto arcNode = store.uri(arc.id);
        std::vector<std::pair<rdf::Node, rdf::Node>> arcTriples;
        store.forEachProperty(arcNode, [&](const rdf::Node& p, const rdf::Node& o)
                              { arcTriples.emplace_back(p, o); });
        for (const auto& [predicate, object] : arcTriples)
            store.remove(arcNode, predicate, object);

        store.remove(store.uri(model->id()), store.uri(vocab::val::arc), arcNode);
    }

    store.registerPrefix("val", vocab::VAL);
    return applyEdit(*this, ctx.readTurtle, store.serialise());
}

OpResult OpDispatcher::connect(const std::string& fromNode, const std::string& fromPort,
                               const std::string& toNode,   const std::string& toPort,
                               std::optional<double> depth)
{
    const auto* model = ctx.readModel ? ctx.readModel() : nullptr;
    if (model == nullptr)
        return OpResult::failure("no circuit is loaded");

    if (model->findElement(fromNode) == nullptr)
        return OpResult::failure("no element with id " + fromNode);
    if (model->findElement(toNode) == nullptr)
        return OpResult::failure("no element with id " + toNode);

    const auto* source = model->findElement(fromNode);
    const auto* dest   = model->findElement(toNode);

    const auto* sourcePort = source->type != nullptr ? source->type->findPort(fromPort) : nullptr;
    const auto* destPort   = dest->type   != nullptr ? dest->type->findPort(toPort)     : nullptr;

    if (sourcePort == nullptr)
        return OpResult::failure(vocab::shortName(source->typeIri) + " has no port '" + fromPort + "'");
    if (destPort == nullptr)
        return OpResult::failure(vocab::shortName(dest->typeIri) + " has no port '" + toPort + "'");
    if (sourcePort->input)
        return OpResult::failure("'" + fromPort + "' is an input port; an arc starts at an output");
    if (! destPort->input)
        return OpResult::failure("'" + toPort + "' is an output port; an arc ends at an input");
    if (sourcePort->control != destPort->control)
        return OpResult::failure("cannot join a " + std::string(sourcePort->control ? "control" : "audio")
                                 + " output to a " + (destPort->control ? "control" : "audio") + " input");

    for (const auto& arc : model->arcs())
        if (arc.fromNode == fromNode && arc.fromPort == fromPort
            && arc.toNode == toNode && arc.toPort == toPort)
            return OpResult::failure("those ports are already connected");

    // A stable id derived from the endpoints, so disconnect can find it again.
    const std::string arcId = model->id() + "-arc-" + vocab::shortName(fromNode) + "-" +
                              fromPort + "-" + vocab::shortName(toNode) + "-" + toPort;

    auto turtle = ctx.readTurtle();
    turtle += "\n<" + arcId + "> <" + vocab::rdf::type + "> <" + vocab::val::Arc + "> ;\n"
              "    <" + vocab::val::from + "> [ <" + vocab::val::node + "> <" + fromNode + "> ; "
              "<" + vocab::val::port + "> \"" + fromPort + "\" ] ;\n"
              "    <" + vocab::val::to + "> [ <" + vocab::val::node + "> <" + toNode + "> ; "
              "<" + vocab::val::port + "> \"" + toPort + "\" ]";

    if (depth.has_value())
        turtle += " ;\n    <" + vocab::valTerm("depth") + "> " + number(*depth);

    turtle += " .\n";
    turtle = withCircuitMember(turtle, model->id(), vocab::val::arc, arcId);

    return applyEdit(*this, ctx.readTurtle, std::move(turtle));
}

OpResult OpDispatcher::disconnect(const std::string& fromNode, const std::string& fromPort,
                                  const std::string& toNode,   const std::string& toPort)
{
    const auto* model = ctx.readModel ? ctx.readModel() : nullptr;
    if (model == nullptr)
        return OpResult::failure("no circuit is loaded");

    const auto match = std::find_if(model->arcs().begin(), model->arcs().end(),
                                    [&](const Arc& arc)
                                    {
                                        return arc.fromNode == fromNode && arc.fromPort == fromPort
                                            && arc.toNode == toNode && arc.toPort == toPort;
                                    });

    if (match == model->arcs().end())
        return OpResult::failure("those ports are not connected");

    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    if (! store.parse(ctx.readTurtle(), "urn:valis:circuit", parseErrors))
        return OpResult::failure("current circuit no longer parses");

    auto arcNode = store.uri(match->id);
    std::vector<std::pair<rdf::Node, rdf::Node>> triples;
    store.forEachProperty(arcNode, [&](const rdf::Node& p, const rdf::Node& o)
                          { triples.emplace_back(p, o); });
    for (const auto& [predicate, object] : triples)
        store.remove(arcNode, predicate, object);

    store.remove(store.uri(model->id()), store.uri(vocab::val::arc), arcNode);

    store.registerPrefix("val", vocab::VAL);
    return applyEdit(*this, ctx.readTurtle, store.serialise());
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

std::vector<ParamInfo> OpDispatcher::listParams() const
{
    std::vector<ParamInfo> result;

    const auto* model = ctx.readModel ? ctx.readModel() : nullptr;
    if (model == nullptr)
        return result;

    for (const auto& binding : model->params())
    {
        ParamInfo info;
        info.slot       = binding.slot;
        info.name       = binding.name;
        info.symbol     = binding.symbol;
        info.targetNode = binding.targetNode;
        info.property   = binding.propertySymbol;

        if (const auto* element = model->findElement(binding.targetNode);
            element != nullptr && element->type != nullptr)
        {
            if (const auto* port = element->type->findProperty(binding.propertySymbol))
            {
                info.minimum = port->minimum;
                info.maximum = port->maximum;
                info.unit    = port->unitSymbol;

                // What is running, not what the Turtle declared: a host or an
                // MCP client may have moved it since the circuit loaded, and
                // set_param followed by get_param has to agree.
                info.value = element->valueOf(binding.propertySymbol);
                if (ctx.engine != nullptr)
                    if (const auto live = ctx.engine->getControl(binding.targetNode,
                                                                 binding.propertySymbol))
                        info.value = *live;
            }
        }

        result.push_back(std::move(info));
    }

    std::sort(result.begin(), result.end(),
              [](const ParamInfo& a, const ParamInfo& b) { return a.slot < b.slot; });
    return result;
}

OpResult OpDispatcher::getParam(int slot) const
{
    for (const auto& param : listParams())
        if (param.slot == slot)
            return OpResult::success(number(param.value));

    return OpResult::failure("no parameter bound to slot " + std::to_string(slot));
}

OpResult OpDispatcher::setParam(int slot, double value)
{
    if (ctx.engine == nullptr)
        return OpResult::failure("no engine attached");

    for (const auto& param : listParams())
    {
        if (param.slot != slot)
            continue;

        const auto clamped = std::clamp(value, param.minimum, param.maximum);
        ctx.engine->setControl(param.targetNode, param.property, static_cast<float>(clamped));

        OpResult result = OpResult::success(number(clamped));
        if (clamped != value)
            result.diagnostics.push_back(
                {"value clamped to [" + number(param.minimum) + ", " + number(param.maximum) + "]",
                 param.targetNode});
        return result;
    }

    return OpResult::failure("no parameter bound to slot " + std::to_string(slot));
}

OpResult OpDispatcher::getDiagnostics() const
{
    const auto* model = ctx.readModel ? ctx.readModel() : nullptr;

    std::string json = "{\"loaded\":";
    json += model != nullptr ? "true" : "false";

    if (model != nullptr)
    {
        json += ",\"elements\":" + std::to_string(model->elements().size());
        json += ",\"arcs\":" + std::to_string(model->arcs().size());
        json += ",\"params\":" + std::to_string(model->params().size());
    }

    if (ctx.engine != nullptr)
        json += ",\"latency\":" + std::to_string(ctx.engine->latencyInSamples());

    json += "}";
    return OpResult::success(std::move(json));
}

}  // namespace valis

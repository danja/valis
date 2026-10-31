// src/ops/OpDispatcher.cpp

#include "valis/Ops.h"

#include "valis/Analysis.h"
#include "valis/CircuitCompiler.h"
#include "valis/CircuitDiff.h"
#include "valis/CircuitHistory.h"
#include "valis/DspElement.h"
#include "valis/Lint.h"
#include "valis/MidiScore.h"
#include "valis/ModalFit.h"
#include "valis/ShotReport.h"
#include "valis/SpectralDistance.h"
#include "valis/Suggest.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"
#include "valis/Vocabulary.h"

#include "dsp/Random.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
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

/// A JSON string literal. Named for what it makes rather than what it does
/// to it, because <iomanip>'s std::quoted would otherwise be found by
/// argument-dependent lookup and win for a std::string argument.
std::string jsonString(std::string_view text) { return "\"" + escape(text) + "\""; }

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

OpResult OpDispatcher::setTurtleUnrecorded(const std::string& turtle)
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
                                  port.input, port.control, port.event, port.buffer,
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

    std::string json = "{\"id\":" + jsonString(model->id()) + ",\"elements\":[";

    bool first = true;
    for (const auto& element : model->elements())
    {
        if (! first) json += ",";
        first = false;

        json += "{\"id\":" + jsonString(element.id)
              + ",\"type\":" + jsonString(element.typeIri)
              + ",\"label\":" + jsonString(element.label);

        // An element the model expanded out of a val:Subcircuit says so, and
        // which voice of a pool it belongs to, so a caller can tell a written
        // element from a generated one.
        if (! element.instance.empty())
        {
            json += ",\"instance\":" + jsonString(element.instance)
                  + ",\"instanceType\":" + jsonString(element.instanceType);
            if (element.voice >= 0)
                json += ",\"voice\":" + std::to_string(element.voice);
        }

        if (! element.options.empty())
        {
            json += ",\"options\":{";
            bool firstOption = true;
            for (const auto& [key, value] : element.options)
            {
                if (! firstOption) json += ",";
                firstOption = false;
                json += jsonString(key) + ":" + jsonString(value);
            }
            json += "}";
        }

        json += ",\"properties\":{";

        bool firstProperty = true;
        for (const auto& [name, value] : element.properties)
        {
            if (! firstProperty) json += ",";
            firstProperty = false;
            json += jsonString(name) + ":" + number(value);
        }
        json += "}}";
    }

    json += "],\"arcs\":[";
    first = true;
    for (const auto& arc : model->arcs())
    {
        if (! first) json += ",";
        first = false;

        json += "{\"id\":" + jsonString(arc.id)
              + ",\"from\":{\"node\":" + jsonString(arc.fromNode) + ",\"port\":" + jsonString(arc.fromPort) + "}"
              + ",\"to\":{\"node\":"   + jsonString(arc.toNode)   + ",\"port\":" + jsonString(arc.toPort)   + "}"
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

namespace {

/// The local names of every element class, for the nearest-name suggestions.
std::vector<std::string> elementNames(const Ontology& ontology)
{
    std::vector<std::string> names;
    for (const auto* type : ontology.types())
        names.push_back(vocab::shortName(type->classIri));
    return names;
}

}  // namespace

OpResult OpDispatcher::describeElement(const std::string& name) const
{
    if (ctx.ontology == nullptr)
        return OpResult::failure("no ontology loaded");

    // val:Ladder, Ladder, the full IRI and ladder all mean the same thing.
    std::string local = name;
    const auto startsWith = [&](std::string_view prefix)
    {
        return local.size() >= prefix.size() &&
               std::equal(prefix.begin(), prefix.end(), local.begin(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    };
    if (startsWith("val:"))
        local = local.substr(4);
    else if (local.rfind(vocab::VAL, 0) == 0)
        local = local.substr(vocab::VAL.size());

    const ElementType* type = ctx.ontology->find(vocab::valTerm(local));
    if (type == nullptr)
    {
        for (const auto* candidate : ctx.ontology->types())
        {
            const auto short_ = vocab::shortName(candidate->classIri);
            if (short_.size() == local.size() &&
                std::equal(short_.begin(), short_.end(), local.begin(),
                           [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
            {
                type = candidate;
                break;
            }
        }
    }

    if (type == nullptr)
        return OpResult::failure("no element class '" + name + "'." +
                                 didYouMean("val:", closestNames(local, elementNames(*ctx.ontology))) +
                                 " list_element_types names every class.");

    std::string json = "{\"class\":" + jsonString(vocab::shortName(type->classIri))
                     + ",\"label\":" + jsonString(type->label)
                     + ",\"linear\":" + (type->linear ? "true" : "false")
                     + ",\"description\":" + jsonString(type->comment) + ",\"ports\":[";

    for (std::size_t i = 0; i < type->ports.size(); ++i)
    {
        const auto& port = type->ports[i];
        json += std::string(i ? "," : "") + "{\"symbol\":" + jsonString(port.symbol)
              + ",\"name\":" + jsonString(port.name)
              + ",\"direction\":" + (port.input ? "\"in\"" : "\"out\"")
              + ",\"rate\":" + jsonString(port.rateName());

        if (port.control && port.input)
        {
            json += ",\"default\":" + number(port.defaultValue) + ",\"minimum\":" + number(port.minimum)
                  + ",\"maximum\":" + number(port.maximum);
            if (! port.unitSymbol.empty())
                json += ",\"unit\":" + jsonString(port.unitSymbol);
            if (port.logarithmic)
                json += ",\"logarithmic\":true";
            if (port.isBinary())
                json += ",\"toggle\":true";
        }

        if (! port.scalePoints.empty())
        {
            json += ",\"choices\":[";
            for (std::size_t c = 0; c < port.scalePoints.size(); ++c)
                json += std::string(c ? "," : "") + "{\"value\":" + number(port.scalePoints[c].first)
                      + ",\"label\":" + jsonString(port.scalePoints[c].second) + "}";
            json += "]";
        }

        if (! port.comment.empty())
            json += ",\"description\":" + jsonString(port.comment);
        json += "}";
    }
    json += "],\"options\":[";

    // The options this class lists, then the one every element takes.
    std::vector<std::string> options = type->options;
    options.push_back("oversampling");
    bool first = true;
    for (const auto& option : options)
    {
        const auto* described = ctx.ontology->findOption(option);

        // The first paragraph only. An option such as oversampling carries a long essay in the
        // ontology, and sending it with every element would bury the ones that matter.
        std::string words = described != nullptr ? described->comment : "";
        if (const auto end = words.find("\n\n"); end != std::string::npos)
            words.resize(end);

        json += std::string(first ? "" : ",") + "{\"name\":" + jsonString(option)
              + ",\"type\":" + jsonString(described != nullptr ? described->range : "")
              + ",\"description\":" + jsonString(words) + "}";
        first = false;
    }
    json += "]}";

    return OpResult::success(std::move(json));
}

OpResult OpDispatcher::addNodeUnrecorded(const std::string& id, const std::string& classIri)
{
    if (ctx.ontology == nullptr)
        return OpResult::failure("no ontology loaded");

    const auto resolved = vocab::expand(classIri);
    if (ctx.ontology->find(resolved) == nullptr)
    {
        std::string local = classIri;
        if (local.rfind("val:", 0) == 0)
            local = local.substr(4);

        return OpResult::failure("unknown or abstract element class " + classIri + "." +
                                 didYouMean("val:", closestNames(local, elementNames(*ctx.ontology))));
    }

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

OpResult OpDispatcher::applyEditsUnrecorded(const std::vector<GraphEdit>& edits)
{
    if (ctx.ontology == nullptr)
        return OpResult::failure("no ontology loaded");
    if (edits.empty())
        return OpResult::failure("there are no edits to apply");
    if (edits.size() > 500)
        return OpResult::failure("at most 500 edits at a time; " + std::to_string(edits.size()) + " were sent");

    const auto* current = ctx.readModel ? ctx.readModel() : nullptr;
    if (current == nullptr || ! ctx.readTurtle || ! ctx.writeTurtle)
        return OpResult::failure("no circuit is loaded");

    // The private copy: the source text and the model built from it. Each edit reads the model and
    // writes the text, exactly as it would on the real circuit, and the copy only has to parse
    // and build, since a state between two edits need not be a circuit that could be installed
    // (the output may be gone until a new one is added).
    struct Scratch
    {
        std::string turtle;
        CircuitModel model;
    } scratch;
    scratch.turtle = ctx.readTurtle();

    {
        rdf::TurtleStore store;
        std::vector<rdf::ParseError> parseErrors;
        std::vector<Diagnostic> ignored;
        if (! store.parse(scratch.turtle, "urn:valis:circuit", parseErrors) ||
            ! scratch.model.build(store, *ctx.ontology, ignored))
            return OpResult::failure("the current circuit no longer builds");
    }

    OpContext inner;
    inner.ontology  = ctx.ontology;
    inner.registry  = ctx.registry;
    inner.readTurtle = [&scratch] { return scratch.turtle; };
    inner.readModel  = [&scratch]() -> const CircuitModel* { return &scratch.model; };
    inner.writeTurtle = [&scratch, this](const std::string& text, std::vector<Diagnostic>& diagnostics)
    {
        rdf::TurtleStore store;
        std::vector<rdf::ParseError> parseErrors;
        if (! store.parse(text, "urn:valis:circuit", parseErrors))
        {
            for (const auto& e : parseErrors)
                diagnostics.push_back({e.message, {}, e.line, e.col});
            return false;
        }

        CircuitModel built;
        if (! built.build(store, *ctx.ontology, diagnostics))
            return false;

        scratch.turtle = text;
        scratch.model = std::move(built);
        return true;
    };
    OpDispatcher trial(inner);

    // An id with no colon belongs to the circuit's own namespace.
    const auto base = [&]
    {
        const auto& id = scratch.model.id();
        const auto hash = id.find('#');
        return hash == std::string::npos ? std::string() : id.substr(0, hash + 1);
    }();
    const auto qualify = [&](const std::string& id)
    {
        return id.find(':') == std::string::npos && ! base.empty() ? base + id : id;
    };

    for (std::size_t i = 0; i < edits.size(); ++i)
    {
        const auto& edit = edits[i];
        OpResult result;
        std::string what;

        switch (edit.kind)
        {
            case GraphEdit::Kind::addNode:
                what = "add_node " + edit.id;
                result = trial.addNode(qualify(edit.id), edit.classIri);
                break;
            case GraphEdit::Kind::removeNode:
                what = "remove_node " + edit.id;
                result = trial.removeNode(qualify(edit.id));
                break;
            case GraphEdit::Kind::connect:
                what = "connect " + edit.fromNode + "." + edit.fromPort + " to " + edit.toNode + "." + edit.toPort;
                result = trial.connect(qualify(edit.fromNode), edit.fromPort, qualify(edit.toNode), edit.toPort, edit.depth);
                break;
            case GraphEdit::Kind::disconnect:
                what = "disconnect " + edit.fromNode + "." + edit.fromPort + " from " + edit.toNode + "." + edit.toPort;
                result = trial.disconnect(qualify(edit.fromNode), edit.fromPort, qualify(edit.toNode), edit.toPort);
                break;
        }

        if (! result.ok)
        {
            std::string why = result.diagnostics.empty() ? std::string("it failed") : result.diagnostics.front().message;
            OpResult failure = OpResult::failure("Edit " + std::to_string(i + 1) + " of " + std::to_string(edits.size())
                                                 + " (" + what + ") failed: " + why + " Nothing was changed.");
            for (std::size_t d = 1; d < result.diagnostics.size(); ++d)
                failure.diagnostics.push_back(result.diagnostics[d]);
            return failure;
        }
    }

    // All of them applied to the copy. Now the circuit as a whole has to compile and load, once.
    const auto previous = ctx.readTurtle();
    OpResult installed;
    installed.ok = ctx.writeTurtle(scratch.turtle, installed.diagnostics);
    if (! installed.ok)
    {
        // The host keeps the text of a refused circuit so that an editor can show it. Here nobody
        // typed it, so the text goes back to what it was, as with any single edit.
        std::vector<Diagnostic> ignored;
        ctx.writeTurtle(previous, ignored);

        std::string why = installed.diagnostics.empty() ? std::string("it was refused") : installed.diagnostics.front().message;
        OpResult failure = OpResult::failure("All " + std::to_string(edits.size()) + " edits applied, but the circuit they make "
                                             "could not be installed: " + why + " Nothing was changed.");
        for (std::size_t d = 1; d < installed.diagnostics.size(); ++d)
            failure.diagnostics.push_back(installed.diagnostics[d]);
        return failure;
    }

    return OpResult::success("{\"applied\":" + std::to_string(edits.size()) + "}");
}

OpResult OpDispatcher::removeNodeUnrecorded(const std::string& id)
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

OpResult OpDispatcher::connectUnrecorded(const std::string& fromNode, const std::string& fromPort,
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

    if (source->type == nullptr || dest->type == nullptr)
        return OpResult::failure("an element of this arc has no known class");

    const auto* sourcePort = source->type->findPort(fromPort);
    const auto* destPort   = dest->type->findPort(toPort);

    if (sourcePort == nullptr)
        return OpResult::failure(source->type->missingPortMessage(fromPort, false));
    if (destPort == nullptr)
        return OpResult::failure(dest->type->missingPortMessage(toPort, true));
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

OpResult OpDispatcher::disconnectUnrecorded(const std::string& fromNode, const std::string& fromPort,
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
// History
// ---------------------------------------------------------------------------

bool OpDispatcher::syncHistory()
{
    if (ctx.history == nullptr || ! ctx.readTurtle)
        return false;

    const auto text = ctx.readTurtle();
    if (! ctx.history->empty() && ctx.history->current()->turtle == text)
        return true;

    // Text that is not a circuit (an editor mid-edit) is not a version to return to.
    if (! validate(text).ok)
        return false;

    ctx.history->sync(text);
    return true;
}

template <typename Run>
OpResult OpDispatcher::recorded(const std::string& label, Run&& run)
{
    if (ctx.history == nullptr || editDepth > 0 || ! ctx.readTurtle)
        return run();

    syncHistory();

    ++editDepth;
    auto result = run();
    --editDepth;

    if (result.ok)
        ctx.history->record(label, ctx.readTurtle());
    return result;
}

OpResult OpDispatcher::setTurtle(const std::string& turtle)
{
    return recorded("set_turtle", [&] { return setTurtleUnrecorded(turtle); });
}

OpResult OpDispatcher::addNode(const std::string& id, const std::string& classIri)
{
    return recorded("add_node " + vocab::shortName(id) + " (" + classIri + ")", [&] { return addNodeUnrecorded(id, classIri); });
}

OpResult OpDispatcher::removeNode(const std::string& id)
{
    return recorded("remove_node " + vocab::shortName(id), [&] { return removeNodeUnrecorded(id); });
}

OpResult OpDispatcher::connect(const std::string& fromNode, const std::string& fromPort,
                               const std::string& toNode, const std::string& toPort, std::optional<double> depth)
{
    return recorded("connect " + vocab::shortName(fromNode) + "." + fromPort + " to " + vocab::shortName(toNode) + "." + toPort,
                    [&] { return connectUnrecorded(fromNode, fromPort, toNode, toPort, depth); });
}

OpResult OpDispatcher::disconnect(const std::string& fromNode, const std::string& fromPort,
                                  const std::string& toNode, const std::string& toPort)
{
    return recorded("disconnect " + vocab::shortName(fromNode) + "." + fromPort + " from " + vocab::shortName(toNode) + "." + toPort,
                    [&] { return disconnectUnrecorded(fromNode, fromPort, toNode, toPort); });
}

OpResult OpDispatcher::applyEdits(const std::vector<GraphEdit>& edits)
{
    return recorded("apply_edits (" + std::to_string(edits.size()) + " edits)", [&] { return applyEditsUnrecorded(edits); });
}

OpResult OpDispatcher::history() const
{
    if (ctx.history == nullptr)
        return OpResult::failure("no history is kept for this circuit");

    std::string json = "{\"current\":" + (ctx.history->empty() ? std::string("null") : std::to_string(ctx.history->current()->id))
                     + ",\"canUndo\":" + (ctx.history->canUndo() ? "true" : "false")
                     + ",\"canRedo\":" + (ctx.history->canRedo() ? "true" : "false") + ",\"versions\":[";

    bool first = true;
    for (const auto& version : ctx.history->versions())
    {
        json += std::string(first ? "" : ",") + "{\"version\":" + std::to_string(version.id)
              + ",\"label\":" + jsonString(version.label)
              + ",\"current\":" + (&version == ctx.history->current() ? "true" : "false") + "}";
        first = false;
    }
    return OpResult::success(json + "]}");
}

OpResult OpDispatcher::undo()
{
    if (ctx.history == nullptr || ! ctx.readTurtle || ! ctx.writeTurtle)
        return OpResult::failure("no history is kept for this circuit");

    // If the text now is not a version and does not compile (an editor mid-edit), the nearest thing to
    // undoing it is to go back to the last text that did: nothing else is lost.
    const bool synced = syncHistory();
    if (ctx.history->empty())
        return OpResult::failure("there is nothing to undo: no version of the circuit has been kept");

    if (! synced)
    {
        const auto& last = *ctx.history->current();
        std::vector<Diagnostic> diagnostics;
        if (! ctx.writeTurtle(last.turtle, diagnostics))
            return OpResult::failure("the circuit's text does not compile and the last good version could not be restored");
        return OpResult::success("{\"version\":" + std::to_string(last.id) + ",\"restored\":" + jsonString(last.label)
                                 + ",\"note\":\"the circuit's text did not compile, so the last version that did was restored\"}");
    }

    if (! ctx.history->canUndo())
        return OpResult::failure("there is nothing to undo: this is the oldest version kept");

    const std::string undone = ctx.history->current()->label;
    const auto* target = ctx.history->undo();

    std::vector<Diagnostic> diagnostics;
    if (! ctx.writeTurtle(target->turtle, diagnostics))
    {
        ctx.history->redo();
        OpResult failure = OpResult::failure("the earlier version could not be installed");
        failure.diagnostics.insert(failure.diagnostics.end(), diagnostics.begin(), diagnostics.end());
        return failure;
    }

    return OpResult::success("{\"version\":" + std::to_string(target->id) + ",\"undone\":" + jsonString(undone)
                             + ",\"canUndo\":" + (ctx.history->canUndo() ? "true" : "false")
                             + ",\"canRedo\":true}");
}

OpResult OpDispatcher::redo()
{
    if (ctx.history == nullptr || ! ctx.writeTurtle)
        return OpResult::failure("no history is kept for this circuit");

    if (! ctx.history->canRedo())
        return OpResult::failure("there is nothing to redo: nothing has been undone since the last change");

    // Text typed since the undo would be lost by moving forward; it becomes the new latest version
    // instead, which ends the redo as any new edit does.
    if (ctx.readTurtle && ! ctx.history->empty() && ctx.history->current()->turtle != ctx.readTurtle() && syncHistory())
        return OpResult::failure("the circuit was changed after the undo, so there is nothing to redo");

    const auto* target = ctx.history->redo();
    std::vector<Diagnostic> diagnostics;
    if (! ctx.writeTurtle(target->turtle, diagnostics))
    {
        ctx.history->undo();
        OpResult failure = OpResult::failure("the later version could not be installed");
        failure.diagnostics.insert(failure.diagnostics.end(), diagnostics.begin(), diagnostics.end());
        return failure;
    }

    return OpResult::success("{\"version\":" + std::to_string(target->id) + ",\"label\":" + jsonString(target->label)
                             + ",\"canUndo\":true,\"canRedo\":" + (ctx.history->canRedo() ? "true" : "false") + "}");
}

OpResult OpDispatcher::diff(std::optional<int> from, std::optional<int> to) const
{
    if (ctx.history == nullptr || ctx.history->empty() || ctx.ontology == nullptr)
        return OpResult::failure("no versions have been kept to compare");

    const auto& versions = ctx.history->versions();
    const CircuitVersion* after = to ? ctx.history->find(*to) : ctx.history->current();
    if (after == nullptr)
        return OpResult::failure("there is no version " + std::to_string(*to) + "; history lists the versions kept");

    const CircuitVersion* before = nullptr;
    if (from)
    {
        before = ctx.history->find(*from);
        if (before == nullptr)
            return OpResult::failure("there is no version " + std::to_string(*from) + "; history lists the versions kept");
    }
    else
    {
        // The one just before `after`.
        for (std::size_t i = 1; i < versions.size(); ++i)
            if (&versions[i] == after)
                before = &versions[i - 1];
        if (before == nullptr)
            return OpResult::failure("version " + std::to_string(after->id) + " is the oldest kept, so there is nothing before it to compare with");
    }

    const auto build = [&](const std::string& text, CircuitModel& model) -> bool
    {
        rdf::TurtleStore store;
        std::vector<rdf::ParseError> parseErrors;
        std::vector<Diagnostic> ignored;
        return store.parse(text, "urn:valis:circuit", parseErrors) && model.build(store, *ctx.ontology, ignored);
    };

    CircuitModel a, b;
    if (! build(before->turtle, a) || ! build(after->turtle, b))
        return OpResult::failure("one of those versions no longer builds, so they cannot be compared");

    const auto changes = diffCircuits(a, b);

    std::string json = "{\"from\":" + std::to_string(before->id) + ",\"to\":" + std::to_string(after->id)
                     + ",\"label\":" + jsonString(after->label) + ",\"count\":" + std::to_string(changes.size()) + ",\"changes\":[";
    for (std::size_t i = 0; i < changes.size(); ++i)
        json += (i ? "," : "") + jsonString(changes[i]);
    return OpResult::success(json + "]}");
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

OpResult OpDispatcher::getSample(const std::string& nodeId) const
{
    const auto* model = ctx.readModel ? ctx.readModel() : nullptr;
    if (model == nullptr)
        return OpResult::failure("no circuit is loaded");

    if (model->findElement(nodeId) == nullptr)
        return OpResult::failure("no element with id " + nodeId);

    if (! ctx.readSample)
        return OpResult::failure("this host cannot report sample files");

    return OpResult::success(ctx.readSample(nodeId));
}

OpResult OpDispatcher::setSample(const std::string& nodeId, const std::string& path)
{
    const auto* model = ctx.readModel ? ctx.readModel() : nullptr;
    if (model == nullptr)
        return OpResult::failure("no circuit is loaded");

    const auto* element = model->findElement(nodeId);
    if (element == nullptr)
        return OpResult::failure("no element with id " + nodeId);

    if (! ctx.writeSample)
        return OpResult::failure("this host cannot load sample files");

    std::string error;
    if (! ctx.writeSample(nodeId, path, error))
        return OpResult::failure(error);

    return OpResult::success(path);
}

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
        info.morphA     = binding.morphA;
        info.morphB     = binding.morphB;

        if (const auto* element = model->findElement(binding.targetNode);
            element != nullptr && element->type != nullptr)
        {
            if (const auto* port = element->type->findProperty(binding.propertySymbol))
            {
                // The binding's own range, where it gives one, is the knob's range: it is what the
                // Controls view and the host parameter use, so it is what a caller must see and be clamped to.
                info.minimum = binding.minimum.value_or(port->minimum);
                info.maximum = binding.maximum.value_or(port->maximum);
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
        if (value < param.minimum || value > param.maximum)
            result.diagnostics.push_back(
                {"value clamped to [" + number(param.minimum) + ", " + number(param.maximum) + "]",
                 param.targetNode});
        return result;
    }

    return OpResult::failure("no parameter bound to slot " + std::to_string(slot));
}

OpResult OpDispatcher::morph(double position)
{
    const double t = std::clamp(position, 0.0, 1.0);

    std::string moved = "[";
    int count = 0;
    for (const auto& param : listParams())
    {
        if (! param.morphA || ! param.morphB)
            continue;

        const auto result = setParam(param.slot, *param.morphA + (*param.morphB - *param.morphA) * t);
        if (! result.ok)
            return result;

        moved += (count++ == 0 ? "" : ",") + std::to_string(param.slot);
    }

    if (count == 0)
        return OpResult::failure("no parameter declares both val:valueA and val:valueB");

    OpResult result = OpResult::success(moved + "]");
    if (!std::equal_to<>{}(t, position))
        result.diagnostics.push_back({"position clamped to [0, 1]", {}});
    return result;
}

OpResult OpDispatcher::randomize(unsigned seed, double amount, const std::vector<int>& keep)
{
    const double blend = std::clamp(amount, 0.0, 1.0);

    dsp::Random random(seed * 2654435761u + 0x9E3779B9u);

    std::string set = "[";
    int count = 0;
    for (const auto& param : listParams())
    {
        // Drawn for every parameter, kept or not, so keeping one does not
        // reshuffle the values of the rest.
        const double draw = param.minimum + (param.maximum - param.minimum) * random.next();

        if (std::find(keep.begin(), keep.end(), param.slot) != keep.end())
            continue;

        const auto result = setParam(param.slot, param.value + (draw - param.value) * blend);
        if (! result.ok)
            return result;

        set += (count++ == 0 ? "" : ",") + std::string("{\"slot\":") + std::to_string(param.slot)
             + ",\"value\":" + result.value + "}";
    }

    if (count == 0)
        return OpResult::failure("the circuit has no parameters to randomise");

    return OpResult::success(set + "]");
}

OpResult OpDispatcher::getProfile() const
{
    const auto* model = ctx.readModel ? ctx.readModel() : nullptr;
    if (model == nullptr)
        return OpResult::failure("no circuit is loaded");

    const auto& profile = model->profile();

    const auto array = [](const std::vector<std::string>& values)
    {
        std::string json = "[";
        for (std::size_t i = 0; i < values.size(); ++i)
            json += (i == 0 ? "" : ",") + jsonString(values[i]);
        return json + "]";
    };

    std::string json = "{\"id\":" + jsonString(model->id())
                     + ",\"declared\":" + (profile.declared ? "true" : "false")
                     + ",\"label\":" + jsonString(profile.label)
                     + ",\"comment\":" + jsonString(profile.comment)
                     + ",\"vendor\":" + jsonString(profile.vendor)
                     + ",\"homepage\":" + jsonString(profile.homepage)
                     + ",\"caution\":" + jsonString(profile.caution)
                     + ",\"role\":" + array(profile.roles)
                     + ",\"accepts\":" + array(profile.accepts)
                     + ",\"produces\":" + array(profile.produces)
                     + ",\"genre\":" + array(profile.genres)
                     + ",\"recommendedBefore\":" + array(profile.recommendedBefore)
                     + ",\"recommendedAfter\":" + array(profile.recommendedAfter)
                     + ",\"companion\":" + array(profile.companions)
                     + "}";

    return OpResult::success(std::move(json));
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

        // The size of a voice pool, where there is one: an element carries the
        // voice it belongs to, and this is how many there are.
        int voices = 0;
        for (const auto& element : model->elements())
            voices = std::max(voices, element.voice + 1);
        json += ",\"voices\":" + std::to_string(voices);
    }

    if (ctx.engine != nullptr)
    {
        json += ",\"latency\":" + std::to_string(ctx.engine->latencyInSamples());
        json += ",\"sampleRate\":" + number(ctx.engine->currentSampleRate());

        // A circuit can load and still have an element that could not do what it was asked,
        // such as a player whose sound file is missing. It plays on, silent or unchanged, so
        // without this list a caller would see a healthy circuit that makes no sound.
        json += ",\"problems\":[";
        bool first = true;
        for (const auto& [node, message] : ctx.engine->notes())
        {
            json += std::string(first ? "" : ",") + "{\"node\":" + jsonString(node)
                  + ",\"message\":" + jsonString(message) + "}";
            first = false;
        }
        json += "]";
    }

    json += "}";
    return OpResult::success(std::move(json));
}

// ---------------------------------------------------------------------------
// Playing, measuring, rendering, saving
// ---------------------------------------------------------------------------

OpResult OpDispatcher::noteOn(int noteNumber, double velocity)
{
    if (ctx.engine == nullptr)
        return OpResult::failure("no engine");
    if (noteNumber < 0 || noteNumber > 127)
        return OpResult::failure("note must be between 0 and 127");

    ctx.engine->noteOn(noteNumber, static_cast<float>(std::clamp(velocity, 0.0, 1.0)));
    return OpResult::success();
}

OpResult OpDispatcher::noteOff(int noteNumber)
{
    if (ctx.engine == nullptr)
        return OpResult::failure("no engine");
    if (noteNumber < 0 || noteNumber > 127)
        return OpResult::failure("note must be between 0 and 127");

    ctx.engine->noteOff(noteNumber);
    return OpResult::success();
}

OpResult OpDispatcher::allNotesOff()
{
    if (ctx.engine == nullptr)
        return OpResult::failure("no engine");

    ctx.engine->allNotesOff();
    return OpResult::success();
}

OpResult OpDispatcher::readOutputs(const std::string& nodeId) const
{
    if (ctx.engine == nullptr)
        return OpResult::failure("no engine");

    const auto* model = ctx.readModel ? ctx.readModel() : nullptr;
    if (model == nullptr)
        return OpResult::failure("no circuit is loaded");

    if (! nodeId.empty() && model->findElement(nodeId) == nullptr)
        return OpResult::failure("no element '" + nodeId + "'");

    std::string json = "{\"nodes\":[";
    bool firstNode = true;

    for (const auto& element : model->elements())
    {
        if (! nodeId.empty() && element.id != nodeId)
            continue;
        if (element.type == nullptr)
            continue;

        const auto outputs = element.type->portsMatching(false, true);
        if (outputs.empty())
            continue;

        if (! firstNode) json += ",";
        firstNode = false;

        json += "{\"id\":" + jsonString(element.id)
              + ",\"type\":" + jsonString(element.typeIri)
              + ",\"outputs\":{";

        bool firstPort = true;
        for (const auto* port : outputs)
        {
            if (! firstPort) json += ",";
            firstPort = false;

            const auto value = ctx.engine->getControlOutput(element.id, port->symbol);
            json += jsonString(port->symbol) + ":";
            json += value ? number(static_cast<double>(*value)) : std::string("null");
        }

        json += "}}";
    }

    json += "]}";
    return OpResult::success(std::move(json));
}

namespace {

/// A model, a compiled circuit and an engine made from the circuit's current text, apart from the
/// one that is playing: for work that needs to run the circuit without touching it.
struct FreshRig
{
    CircuitModel model;
    CompiledCircuit compiled;
    ValisEngine engine;
};

/// Builds a private model, compiler and engine from `turtle`. Nothing live is read, so it is the message thread's
/// only because it parses and compiles; on a worker it works from a snapshot's text (see CircuitSnapshot).
OpResult buildFreshRig(const OpContext& ctx, const std::string& turtle, double sampleRate, int block, FreshRig& rig)
{
    if (ctx.ontology == nullptr || ctx.registry == nullptr)
        return OpResult::failure("no ontology or registry");
    if (turtle.empty())
        return OpResult::failure("no circuit is loaded");

    rdf::TurtleStore store;
    std::vector<rdf::ParseError> parseErrors;
    if (! store.parse(turtle, "urn:valis:render", parseErrors))
    {
        OpResult result = OpResult::failure("the circuit will not parse");
        for (const auto& e : parseErrors)
            result.diagnostics.push_back({e.message, {}, e.line, e.col});
        return result;
    }

    std::vector<Diagnostic> diagnostics;
    if (! rig.model.build(store, *ctx.ontology, diagnostics))
    {
        OpResult result = OpResult::failure("the circuit will not build");
        result.diagnostics = std::move(diagnostics);
        return result;
    }

    CircuitCompiler compiler;
    if (! compiler.compile(rig.model, *ctx.ontology, rig.compiled, diagnostics))
    {
        OpResult result = OpResult::failure("the circuit will not compile");
        result.diagnostics = std::move(diagnostics);
        return result;
    }

    rig.engine.prepare(sampleRate, block);

    std::string error;
    if (! rig.engine.load(rig.compiled, *ctx.registry, error))
        return OpResult::failure("the circuit will not load: " + error);

    return OpResult::success();
}

}  // namespace

struct RenderJob::Impl
{
    std::string path;
    double seconds = 0.0, sampleRate = 0.0, velocity = 1.0, tempo = 120.0;
    int note = -1;
    bool rolling = false, hasMidi = false;
    std::vector<TimedNote> score;
    FreshRig rig;
};

RenderJob::RenderJob(std::unique_ptr<Impl> i) : impl(std::move(i)) {}
RenderJob::~RenderJob() = default;

std::unique_ptr<RenderJob> OpDispatcher::beginRender(const std::string& path, double seconds, double sampleRate,
                                                     int note, double velocity,
                                                     const std::string& midiPath, double tempo, bool rolling,
                                                     OpResult& failure) const
{
    if (path.empty())
    {
        failure = OpResult::failure("render needs an output path");
        return nullptr;
    }

    constexpr int kBlock = 512;

    auto impl = std::make_unique<RenderJob::Impl>();
    impl->path       = path;
    impl->tempo      = std::clamp(tempo, 20.0, 400.0);
    impl->seconds    = std::clamp(seconds, 0.05, 300.0);
    impl->sampleRate = std::clamp(sampleRate, 8000.0, 192000.0);
    impl->note       = note;
    impl->velocity   = velocity;
    impl->rolling    = rolling;
    impl->hasMidi    = ! midiPath.empty();

    if (impl->hasMidi)
    {
        std::string midiError;
        if (! readMidiScore(midiPath, impl->tempo, impl->sampleRate, impl->score, midiError))
        {
            failure = OpResult::failure(midiError);
            return nullptr;
        }
    }

    // A fresh model, compiler and engine, so rendering neither reads nor
    // disturbs whatever the plugin is currently playing.
    if (! ctx.readTurtle)
    {
        failure = OpResult::failure("no circuit is loaded");
        return nullptr;
    }
    if (auto built = buildFreshRig(ctx, ctx.readTurtle(), impl->sampleRate, kBlock, impl->rig); ! built.ok)
    {
        failure = std::move(built);
        return nullptr;
    }

    return std::unique_ptr<RenderJob>(new RenderJob(std::move(impl)));
}

OpResult RenderJob::run()
{
    constexpr int kBlock = 512;

    const auto& path       = impl->path;
    const double seconds    = impl->seconds;
    const double sampleRate = impl->sampleRate;
    const double tempo      = impl->tempo;
    const bool rolling      = impl->rolling;
    const auto& score       = impl->score;
    std::size_t nextEvent = 0;
    auto& engine   = impl->rig.engine;
    auto& compiled = impl->rig.compiled;

    const auto total = static_cast<int>(seconds * sampleRate);
    std::vector<float> left(static_cast<std::size_t>(total), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(total), 0.0f);
    const std::vector<float> silence(kBlock, 0.0f);

    // The note fires at the start and is released with a fifth of the render
    // left, so an envelope's release is part of what is measured.
    const int noteOffAt = static_cast<int>(static_cast<double>(total) * 0.8);

    for (int at = 0; at < total; at += kBlock)
    {
        const int n = std::min(kBlock, total - at);

        // A score event fires in the block that contains it, at its own offset.
        while (nextEvent < score.size() && score[nextEvent].sample < at + n)
        {
            const auto& e = score[nextEvent++];
            const int offset = static_cast<int>(std::max<long long>(0, e.sample - at));
            if (e.on) engine.queueNoteOn(e.note, e.velocity, offset);
            else      engine.queueNoteOff(e.note, offset);
        }

        TransportInfo transport;
        transport.playing     = rolling;
        transport.tempoBpm    = tempo;
        transport.ppqPosition = rolling ? static_cast<double>(at) / sampleRate * tempo / 60.0 : 0.0;
        engine.setTransport(transport);

        if (! impl->hasMidi && impl->note >= 0 && impl->note <= 127)
        {
            if (at == 0)
                engine.queueNoteOn(impl->note, static_cast<float>(std::clamp(impl->velocity, 0.0, 1.0)), 0);

            // Fires in the block that contains it, at its own offset.
            if (noteOffAt >= at && noteOffAt < at + n)
                engine.queueNoteOff(impl->note, noteOffAt - at);
        }

        engine.process(silence.data(), left.data() + at, right.data() + at, n);
    }

    juce::File outputFile(juce::File::isAbsolutePath(juce::String(path))
                              ? juce::File(juce::String(path))
                              : juce::File::getCurrentWorkingDirectory()
                                    .getChildFile(juce::String(path)));
    outputFile.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(outputFile.createOutputStream());
    if (stream == nullptr)
        return OpResult::failure("cannot write " + outputFile.getFullPathName().toStdString());

    const auto writerOptions = juce::AudioFormatWriterOptions()
                                   .withSampleRate(sampleRate)
                                   .withNumChannels(2)
                                   .withBitsPerSample(24);

    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream, writerOptions));
    if (writer == nullptr)
        return OpResult::failure("cannot create a wav writer");

    const float* channels[] = { left.data(), right.data() };
    writer->writeFromFloatArrays(channels, 2, total);
    writer.reset();

    // Measured over both channels, so a circuit that is silent on the left and loud on the
    // right is not reported as silent. The analysis says what kind of sound it is.
    const auto report = analyseSignal(left, right, sampleRate);
    const double peak = report.peak;
    const double rms = report.rms;
    const bool finite = report.nonFinite == 0;

    std::string json = "{\"path\":" + jsonString(outputFile.getFullPathName().toStdString())
                     + ",\"seconds\":" + number(seconds)
                     + ",\"sampleRate\":" + number(sampleRate)
                     + ",\"nodes\":" + std::to_string(compiled.nodes.size())
                     + ",\"latency\":" + std::to_string(engine.latencyInSamples())
                     + ",\"peak\":" + number(peak)
                     + ",\"rms\":" + number(rms)
                     + ",\"finite\":" + (finite ? "true" : "false")
                     + ",\"analysis\":" + reportToJson(report)
                     + "}";

    return OpResult::success(std::move(json));
}


OpResult OpDispatcher::render(const std::string& path, double seconds, double sampleRate,
                              int note, double velocity,
                              const std::string& midiPath, double tempo, bool rolling) const
{
    OpResult failure;
    auto job = beginRender(path, seconds, sampleRate, note, velocity, midiPath, tempo, rolling, failure);
    return job != nullptr ? job->run() : failure;
}

OpResult OpDispatcher::lint() const
{
    const auto* model = ctx.readModel ? ctx.readModel() : nullptr;
    if (model == nullptr)
        return OpResult::failure("no circuit is loaded");

    return OpResult::success(lintToJson(lintCircuit(*model)));
}

namespace {
OpResult fromShot(const ShotResult& r)
{
    return r.ok ? OpResult::success(r.json) : OpResult::failure(r.error);
}
}  // namespace

OpResult OpDispatcher::measure(const std::string& path) const
{
    return fromShot(shotMeasure(path));
}

OpResult OpDispatcher::compare(const std::string& pathA, const std::string& pathB) const
{
    return fromShot(shotCompare(pathA, pathB));
}

OpResult OpDispatcher::listen(const std::string& path, const std::vector<std::string>& queries) const
{
    return fromShot(shotListen(path, queries));
}

OpResult OpDispatcher::soundsLike(const std::string& path, const std::string& sentence) const
{
    return fromShot(shotIs(path, sentence));
}

/// A sound file mixed to mono, up to `maxSeconds` of it, at the file's own sample rate (held to 8 kHz to 192 kHz).
bool readMonoFile(const std::string& path, double maxSeconds, double& rate, std::vector<float>& mono, std::string& error,
                  double* fileSeconds = nullptr)
{
    const juce::File file(juce::File::isAbsolutePath(juce::String(path))
                              ? juce::File(juce::String(path))
                              : juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(path)));
    if (! file.existsAsFile())
    {
        error = "no such file: " + file.getFullPathName().toStdString();
        return false;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "not an audio file this build can read: " + file.getFullPathName().toStdString();
        return false;
    }

    rate = std::clamp(reader->sampleRate, 8000.0, 192000.0);
    if (fileSeconds != nullptr)
        *fileSeconds = static_cast<double>(reader->lengthInSamples) / std::max(1.0, reader->sampleRate);
    const auto frames = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples,
                                                               static_cast<juce::int64>(maxSeconds * rate)));
    if (frames <= 0)
    {
        error = "the file holds no audio";
        return false;
    }

    juce::AudioBuffer<float> recorded(static_cast<int>(std::min<unsigned>(reader->numChannels, 2u)), frames);
    reader->read(&recorded, 0, frames, 0, true, recorded.getNumChannels() > 1);
    mono.assign(static_cast<std::size_t>(frames), 0.0f);
    for (int i = 0; i < frames; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < recorded.getNumChannels(); ++c)
            sum += recorded.getSample(c, i);
        mono[static_cast<std::size_t>(i)] = sum / static_cast<float>(recorded.getNumChannels());
    }
    return true;
}

CircuitSnapshot OpDispatcher::snapshot() const
{
    CircuitSnapshot snap;
    snap.loaded = ctx.readModel && ctx.readModel() != nullptr && ctx.readTurtle;
    if (snap.loaded)
    {
        snap.turtle = ctx.readTurtle();
        snap.params = listParams();
    }
    return snap;
}

OpResult OpDispatcher::fitParams(const std::string& targetPath, int note, double velocity, double seconds,
                                 int evaluations, const std::vector<int>& slots, double holdSeconds) const
{
    return fitParams(snapshot(), targetPath, note, velocity, seconds, evaluations, slots, holdSeconds);
}

OpResult OpDispatcher::fitParams(const CircuitSnapshot& snap, const std::string& targetPath, int note, double velocity,
                                 double seconds, int evaluations, const std::vector<int>& slots,
                                 double holdSeconds) const
{
    if (targetPath.empty())
        return OpResult::failure("fit needs the path of the recording to match");
    if (note < 0 || note > 127)
        return OpResult::failure("fit needs a MIDI note to play, 0 to 127");

    seconds     = std::clamp(seconds, 0.25, 10.0);
    evaluations = std::clamp(evaluations, 2, 400);

    // The recording, mixed to mono, as much of it as will be rendered.
    double rate = 0.0;
    std::vector<float> target;
    std::string readError;
    double fileSeconds = 0.0;
    if (! readMonoFile(targetPath, seconds, rate, target, readError, &fileSeconds))
        return OpResult::failure(readError);
    const auto total = static_cast<int>(target.size());
    if (total < 2048)
        return OpResult::failure("the recording is too short to match (under 2048 samples)");
    const juce::File file(juce::File::isAbsolutePath(juce::String(targetPath))
                              ? juce::File(juce::String(targetPath))
                              : juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(targetPath)));

    const auto& all = snap.params;
    if (all.empty())
        return OpResult::failure("the circuit has no val:Param to search; bind the settings that matter first");

    // Which parameters move. The search is over the unit interval for each, evenly in the logarithm where the
    // range spans decades, as a person sweeps a frequency; a bound of plus or minus a million means \"unbounded\" and is
    // held to a thousand.
    struct Axis { ParamInfo param; double lo = 0.0, hi = 1.0; bool logScale = false; };
    std::vector<Axis> axes;
    for (const auto& p : all)
    {
        if (! slots.empty() && std::find(slots.begin(), slots.end(), p.slot) == slots.end())
            continue;
        Axis a{p, p.minimum <= -1.0e5 ? -1000.0 : p.minimum, p.maximum >= 1.0e5 ? 1000.0 : p.maximum, false};
        if (! (a.hi > a.lo))
            continue;
        a.logScale = a.lo > 0.0 && a.hi / a.lo >= 50.0;
        axes.push_back(a);
    }
    if (axes.empty())
        return OpResult::failure("none of the requested slots is a parameter with a range to search");

    const auto toValue = [](const Axis& a, double u)
    {
        return a.logScale ? a.lo * std::pow(a.hi / a.lo, u) : a.lo + (a.hi - a.lo) * u;
    };
    const auto toUnit = [](const Axis& a, double v)
    {
        v = std::clamp(v, a.lo, a.hi);
        return a.logScale ? std::log(v / a.lo) / std::log(a.hi / a.lo) : (v - a.lo) / (a.hi - a.lo);
    };

    constexpr int kBlock = 512;
    const std::vector<float> silence(kBlock, 0.0f);
    // When the recording's note was released. Named by the caller; else at 80% of a whole recording, as render does;
    // else not at all inside the window, because only the start of a longer recording is being matched and its
    // release came after that.
    const bool partOfLonger = fileSeconds > static_cast<double>(total) / rate + 0.05;
    const int noteOffAt = holdSeconds >= 0.0 ? static_cast<int>(holdSeconds * rate)
                        : partOfLonger       ? total + 2 * kBlock
                                             : static_cast<int>(static_cast<double>(total) * 0.8);

    // One render of the circuit with every searched parameter at the given unit position.
    const auto distanceAt = [&](const std::vector<double>& unit, OpResult& failure) -> double
    {
        FreshRig rig;
        if (auto built = buildFreshRig(ctx, snap.turtle, rate, kBlock, rig); ! built.ok)
        {
            failure = built;
            return -1.0;
        }

        for (std::size_t i = 0; i < axes.size(); ++i)
        {
            const auto value = static_cast<float>(toValue(axes[i], unit[i]));
            rig.engine.setControl(axes[i].param.targetNode, axes[i].param.property, value);
            for (const auto& binding : rig.model.params())
                if (binding.slot == axes[i].param.slot)
                    for (const auto& [node, port] : binding.alsoTargets)
                        rig.engine.setControl(node, port, value);
        }

        std::vector<float> left(static_cast<std::size_t>(total), 0.0f), right(static_cast<std::size_t>(total), 0.0f);
        for (int at = 0; at < total; at += kBlock)
        {
            const int n = std::min(kBlock, total - at);
            if (at == 0)
                rig.engine.queueNoteOn(note, static_cast<float>(std::clamp(velocity, 0.0, 1.0)), 0);
            if (noteOffAt >= at && noteOffAt < at + n)
                rig.engine.queueNoteOff(note, noteOffAt - at);
            rig.engine.process(silence.data(), left.data() + at, right.data() + at, n);
        }

        std::vector<float> mono(static_cast<std::size_t>(total));
        for (std::size_t i = 0; i < mono.size(); ++i)
        {
            mono[i] = 0.5f * (left[i] + right[i]);
            if (! std::isfinite(mono[i]))
                return 1.0e6;   // a diverged render is as far from the target as it gets
        }
        return spectralDistance(target, mono, rate).meanDb;
    };

    std::vector<double> startPoint;
    for (const auto& a : axes)
        startPoint.push_back(toUnit(a, a.param.value));

    OpResult failure;
    const double startDistance = distanceAt(startPoint, failure);
    if (startDistance < 0.0)
        return failure;

    dsp::Random random(0x5EA2C4u);   // fixed, so the same call finds the same settings
    const auto gaussian = [&random] { return random.bipolar() + random.bipolar() + random.bipolar(); };
    const auto uniform = [&random] { return 0.5 * (static_cast<double>(random.bipolar()) + 1.0); };

    struct Candidate { std::vector<double> point; double distance = 0.0; };
    int used = 1;
    bool failed = false;

    // (1 + 4) evolution strategy from one candidate: four children per generation, the step growing after a gain and
    // shrinking after none. Spends at most `budget` renders.
    constexpr int kChildren = 4;
    const auto climb = [&](Candidate from, int budget) -> Candidate
    {
        double step = 0.2;
        int spent = 0;
        while (spent < budget && ! failed)
        {
            bool improved = false;
            for (int child = 0; child < kChildren && spent < budget; ++child, ++spent, ++used)
            {
                auto trial = from.point;
                for (auto& u : trial)
                    u = std::clamp(u + step * static_cast<double>(gaussian()), 0.0, 1.0);

                const double d = distanceAt(trial, failure);
                if (d < 0.0)
                {
                    failed = true;
                    break;
                }
                if (d < from.distance)
                {
                    from.distance = d;
                    from.point = std::move(trial);
                    improved = true;
                }
            }
            step = improved ? std::min(0.5, step * 1.3) : std::max(0.003, step * 0.75);
        }
        return from;
    };

    // One climb from where the circuit is now cannot get out of a poor basin, so with enough renders a fifth of them
    // try a seeded spread of points over the whole range first, and the search climbs from the best of those and the
    // start. With fewer, the single climb is all there is room for.
    std::vector<Candidate> starts{{startPoint, startDistance}};
    if (evaluations >= 24)
    {
        const int samples = evaluations / 5;
        for (int i = 0; i < samples && ! failed; ++i, ++used)
        {
            Candidate c;
            for (std::size_t k = 0; k < axes.size(); ++k)
                c.point.push_back(uniform());
            c.distance = distanceAt(c.point, failure);
            if (c.distance < 0.0)
                return failure;
            starts.push_back(std::move(c));
        }
        std::sort(starts.begin(), starts.end(), [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
        starts.resize(std::min<std::size_t>(starts.size(), evaluations >= 100 ? 3 : 2));
    }

    Candidate bestFound = starts.front();
    const int climbBudget = evaluations - used;
    for (std::size_t i = 0; i < starts.size() && ! failed; ++i)
    {
        const int share = climbBudget / static_cast<int>(starts.size()) + (i == 0 ? climbBudget % static_cast<int>(starts.size()) : 0);
        const auto result = climb(starts[i], share);
        if (result.distance < bestFound.distance)
            bestFound = result;
    }
    if (failed)
        return failure;

    const double best = bestFound.distance;
    const auto& parent = bestFound.point;

    std::string found = "[";
    for (std::size_t i = 0; i < axes.size(); ++i)
        found += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(axes[i].param.slot)
               + ",\"name\":" + jsonString(axes[i].param.name)
               + ",\"node\":" + jsonString(axes[i].param.targetNode)
               + ",\"property\":" + jsonString(axes[i].param.property)
               + ",\"start\":" + number(axes[i].param.value)
               + ",\"best\":" + number(toValue(axes[i], parent[i])) + "}";
    found += "]";

    // A match that is still far off may be a poor search, or a circuit that cannot make this sound with these knobs.
    std::string notes;
    if (best > 1.0)
        notes = ",\"notes\":[" + jsonString("The best match is still " + number(best) + " dB from the recording. The search may be stuck "
                                             "(try more evaluations or fewer slots), the note may have been held for a different time "
                                             "(try hold), or these parameters may not be able to make this sound.") + "]";

    return OpResult::success("{\"target\":" + jsonString(file.getFullPathName().toStdString())
                             + ",\"note\":" + std::to_string(note)
                             + ",\"seconds\":" + number(static_cast<double>(total) / rate)
                             + ",\"evaluations\":" + std::to_string(used)
                             + ",\"startDistanceDb\":" + number(startDistance)
                             + ",\"bestDistanceDb\":" + number(best)
                             + ",\"startsClimbed\":" + std::to_string(starts.size())
                             + ",\"releaseAtSeconds\":" + (noteOffAt >= total ? std::string("null") : number(static_cast<double>(noteOffAt) / rate))
                             + ",\"params\":" + found + notes + "}");
}

OpResult OpDispatcher::fitModes(const std::string& path, int modes, double seconds) const
{
    if (path.empty())
        return OpResult::failure("fit_modes needs the path of a recording of a struck sound");

    modes   = std::clamp(modes, 1, 64);
    seconds = std::clamp(seconds, 0.25, 10.0);

    double rate = 0.0;
    std::vector<float> mono;
    std::string error;
    if (! readMonoFile(path, seconds, rate, mono, error))
        return OpResult::failure(error);

    const auto fitted = valis::fitModes(mono, rate, modes);
    if (fitted.empty())
        return OpResult::failure("no modes found: the recording is silent, shorter than 4096 samples, or has no "
                                 "tonal peaks. It should be a struck sound, a bell or a drum, not a held note or noise.");

    // How near the fitted bank, struck by an impulse, is to the recording from its onset.
    float loudest = 0.0f;
    for (const float x : mono)
        loudest = std::max(loudest, std::abs(x));
    std::size_t onset = 0;
    while (onset < mono.size() && std::abs(mono[onset]) < loudest * 0.1f)
        ++onset;
    const std::vector<float> heard(mono.begin() + static_cast<std::ptrdiff_t>(onset), mono.end());
    const auto distance = spectralDistance(heard, renderModes(fitted, rate, heard.size()), rate).meanDb;

    const auto text = modesToString(fitted);
    return OpResult::success("{\"file\":" + jsonString(path)
                             + ",\"sampleRate\":" + number(rate)
                             + ",\"count\":" + std::to_string(fitted.size())
                             + ",\"modes\":" + jsonString(text)
                             + ",\"distanceDb\":" + number(distance)
                             + ",\"turtle\":" + jsonString(":bank a val:FreeModalBank ; val:modes \"" + text + "\" .")
                             + "}");
}

OpResult OpDispatcher::checkStability(int trials, double seconds, int note) const
{
    return checkStability(snapshot(), trials, seconds, note);
}

OpResult OpDispatcher::checkStability(const CircuitSnapshot& snap, int trials, double seconds, int note) const
{
    trials  = std::clamp(trials, 1, 32);
    seconds = std::clamp(seconds, 0.25, 10.0);

    constexpr double kRate = 48000.0;
    constexpr int    kBlock = 512;

    const auto& params = snap.params;
    if (! snap.loaded)
        return OpResult::failure("no circuit is loaded");

    // One input for every trial: noise and a tone, quiet enough that gain is what is being tested.
    const auto total = static_cast<int>(seconds * kRate);
    std::vector<float> input(static_cast<std::size_t>(total));
    {
        dsp::Random noise(0xC0FFEEu);
        for (int i = 0; i < total; ++i)
            input[static_cast<std::size_t>(i)] = 0.15f * noise.bipolar()
                + 0.15f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 220.0 * i / kRate));
    }

    // A setting for each parameter: spread evenly, or evenly in the logarithm where the range spans
    // decades, as a person sweeps a frequency. A range of plus or minus a million means "unbounded"
    // and is held to a thousand.
    const auto draw = [](dsp::Random& random, const ParamInfo& p)
    {
        const double lo = std::max(p.minimum, -1000.0), hi = std::min(p.maximum, 1000.0);
        if (! (hi > lo))
            return lo;
        const double t = static_cast<double>(random.next());
        if (lo > 0.0 && hi / lo >= 50.0)
            return lo * std::pow(hi / lo, t);
        return lo + (hi - lo) * t;
    };

    struct Trial
    {
        int seed = 0;
        std::string verdict;
        double peak = 0.0, rms = 0.0;
        long nonFinite = 0;
        std::vector<double> before, after;
    };
    std::vector<Trial> results;

    int blewUp = 0, nonFiniteRuns = 0, clipping = 0, silent = 0;

    for (int t = 0; t < trials; ++t)
    {
        Trial trial;
        trial.seed = t;   // 0 is the declared settings

        FreshRig rig;
        if (auto built = buildFreshRig(ctx, snap.turtle, kRate, kBlock, rig); ! built.ok)
            return built;

        dsp::Random random(static_cast<std::uint32_t>(t) * 2654435761u + 0x9E3779B9u);
        if (t > 0)
        {
            for (const auto& p : params) trial.before.push_back(draw(random, p));
            for (const auto& p : params) trial.after.push_back(draw(random, p));
        }

        const auto apply = [&](const std::vector<double>& values)
        {
            for (std::size_t i = 0; i < params.size() && i < values.size(); ++i)
            {
                rig.engine.setControl(params[i].targetNode, params[i].property, static_cast<float>(values[i]));

                for (const auto& binding : rig.model.params())
                    if (binding.slot == params[i].slot)
                        for (const auto& [node, port] : binding.alsoTargets)
                            rig.engine.setControl(node, port, static_cast<float>(values[i]));
            }
        };
        apply(trial.before);

        std::vector<float> left(static_cast<std::size_t>(kBlock)), right(static_cast<std::size_t>(kBlock));
        const int jumpAt = total / 2;
        const int noteOffAt = static_cast<int>(static_cast<double>(total) * 0.8);
        double sumSquares = 0.0;
        long counted = 0;

        for (int at = 0; at < total; at += kBlock)
        {
            const int n = std::min(kBlock, total - at);

            if (note >= 0 && note <= 127)
            {
                if (at == 0)
                    rig.engine.queueNoteOn(note, 0.9f, 0);
                if (noteOffAt >= at && noteOffAt < at + n)
                    rig.engine.queueNoteOff(note, noteOffAt - at);
            }

            // Every parameter at once, as a preset change or a scene would.
            if (t > 0 && jumpAt >= at && jumpAt < at + n)
                apply(trial.after);

            rig.engine.process(input.data() + at, left.data(), right.data(), n);

            for (int i = 0; i < n; ++i)
                for (const float s : {left[static_cast<std::size_t>(i)], right[static_cast<std::size_t>(i)]})
                {
                    if (! std::isfinite(s)) { ++trial.nonFinite; continue; }
                    trial.peak = std::max(trial.peak, static_cast<double>(std::abs(s)));
                    sumSquares += static_cast<double>(s) * s;
                    ++counted;
                }
        }
        trial.rms = counted > 0 ? std::sqrt(sumSquares / static_cast<double>(counted)) : 0.0;

        if (trial.nonFinite > 0)      { trial.verdict = "non-finite"; ++nonFiniteRuns; }
        else if (trial.peak > 100.0)  { trial.verdict = "blows-up";   ++blewUp; }
        else if (trial.peak > 1.0)    { trial.verdict = "clips";      ++clipping; }
        else if (trial.peak < 1.0e-6) { trial.verdict = "silent";     ++silent; }
        else                          { trial.verdict = "ok"; }

        results.push_back(std::move(trial));
    }

    const auto db = [](double v) { return 20.0 * std::log10(std::max(v, 1.0e-6)); };

    std::string json = "{\"trials\":" + std::to_string(trials) + ",\"seconds\":" + number(seconds)
                     + ",\"parameters\":" + std::to_string(params.size())
                     + ",\"blowUps\":" + std::to_string(blewUp) + ",\"nonFinite\":" + std::to_string(nonFiniteRuns)
                     + ",\"clipping\":" + std::to_string(clipping) + ",\"silent\":" + std::to_string(silent)
                     + ",\"results\":[";

    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const auto& r = results[i];
        json += std::string(i ? "," : "") + "{\"seed\":" + std::to_string(r.seed) + ",\"verdict\":" + jsonString(r.verdict)
              + ",\"peakDb\":" + number(db(r.peak)) + ",\"rmsDb\":" + number(db(r.rms));
        if (r.nonFinite > 0)
            json += ",\"nonFiniteSamples\":" + std::to_string(r.nonFinite);

        // The settings, for a run that is not fine, so that it can be set up again.
        if (r.verdict != "ok" && ! r.before.empty())
        {
            json += ",\"settings\":[";
            for (std::size_t p = 0; p < params.size(); ++p)
                json += std::string(p ? "," : "") + "{\"slot\":" + std::to_string(params[p].slot)
                      + ",\"name\":" + jsonString(params[p].name)
                      + ",\"start\":" + number(r.before[p]) + ",\"afterJump\":" + number(r.after[p]) + "}";
            json += "]";
        }
        json += "}";
    }
    json += "],\"notes\":[";

    std::vector<std::string> notes;
    const auto count = [&](int n) { return std::to_string(n) + " of " + std::to_string(trials); };
    if (params.empty())
        notes.push_back("No parameters are bound, so only the declared settings were tried. Bind the controls a player would reach "
                        "with val:Param to test them.");
    if (nonFiniteRuns > 0)
        notes.push_back(count(nonFiniteRuns) + " runs produced values that are not numbers (NaN or infinity): something becomes "
                        "unstable at some setting. The settings are listed for each.");
    if (blewUp > 0)
        notes.push_back(count(blewUp) + " runs peaked above +40 dBFS: a combination of settings multiplies gain without limit. "
                        "Put a val:Limiter before the output, or narrow the parameter ranges.");
    if (clipping > 0)
        notes.push_back(count(clipping) + " runs clipped (peak above 0 dBFS) but stayed bounded.");
    if (silent > 0)
        notes.push_back(count(silent) + " runs were silent: some setting closes the sound off entirely.");
    if (nonFiniteRuns == 0 && blewUp == 0)
        notes.push_back("Every run stayed finite and below +40 dBFS, with every parameter changing at once partway through.");
    for (std::size_t i = 0; i < notes.size(); ++i)
        json += (i ? "," : "") + jsonString(notes[i]);

    return OpResult::success(json + "]}");
}

OpResult OpDispatcher::saveFile(const std::string& path) const
{
    if (! ctx.readTurtle)
        return OpResult::failure("no circuit is loaded");
    if (path.empty())
        return OpResult::failure("save needs a path");

    juce::File file(juce::File::isAbsolutePath(juce::String(path))
                        ? juce::File(juce::String(path))
                        : juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(path)));

    if (! file.getParentDirectory().isDirectory())
        return OpResult::failure("no directory " +
                                 file.getParentDirectory().getFullPathName().toStdString());

    // Line endings explicitly: replaceWithText defaults to CRLF, which would
    // rewrite every line of a document the user wrote.
    if (! file.replaceWithText(juce::String(ctx.readTurtle()), false, false, "\n"))
        return OpResult::failure("cannot write " + file.getFullPathName().toStdString());

    return OpResult::success(file.getFullPathName().toStdString());
}

}  // namespace valis

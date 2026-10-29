// src/ops/Lint.cpp

#include "valis/Lint.h"

#include "valis/CircuitModel.h"
#include "valis/Ontology.h"
#include "valis/Suggest.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace valis {

namespace {

std::string quote(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out + "\"";
}

std::string shortId(const std::string& iri) { return vocab::shortName(iri); }

std::string number(double v)
{
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.6g", v);
    return buffer;
}

/// Elements that exist to be looked at or to send events to the host, so that nothing
/// downstream of them is correct.
bool isDeadEndByDesign(const std::string& implementation)
{
    return implementation == "Oscilloscope" || implementation == "FreqAnalyzer"
        || implementation == "NoteOut";
}

/// Elements whose audio inputs are all optional: a modulation input, or a recorder that
/// can play a file instead.
bool hasOptionalInputs(const std::string& implementation)
{
    return implementation == "Granulator" || implementation == "GrainBuffer"
        || implementation == "FMOperator" || implementation == "Bow"
        || implementation == "Input" || implementation == "Output";
}

}  // namespace

std::vector<LintFinding> lintCircuit(const CircuitModel& model)
{
    using Severity = LintFinding::Severity;
    std::vector<LintFinding> findings;
    const auto add = [&](Severity severity, std::string code, std::string node, std::string port, std::string message)
    {
        findings.push_back({severity, std::move(code), std::move(node), std::move(port), std::move(message)});
    };

    const auto& elements = model.elements();
    const auto& arcs = model.arcs();

    // Which control ports have an arc reaching them, and from where.
    std::map<std::pair<std::string, std::string>, std::string> controlled;
    for (const auto& arc : arcs)
        if (arc.control)
            controlled.emplace(std::make_pair(arc.toNode, arc.toPort), arc.fromNode);

    // 1. A fixed value set on a port that an arc replaces every block.
    for (const auto& element : elements)
    {
        // Inside a subcircuit, declared values are the definition's defaults and an exposed
        // port is routinely driven from outside, so this would only be noise there.
        if (! element.instance.empty())
            continue;

        for (const auto& [port, value] : element.properties)
        {
            const auto reaching = controlled.find({element.id, port});
            if (reaching == controlled.end())
                continue;

            add(Severity::warning, "control-overrides-value", element.id, port,
                "val:" + port + " is set to " + number(value) + " on " + shortId(element.id) + ", but a control arc from "
                + shortId(reaching->second) + " replaces it every block, so the value is never used. Put the resting "
                "value in the control source instead (a val:Scale's min, or a val:Drift's centre).");
        }
    }

    // 1b. A val: property the element does not know. Any val: property that is not a control
    // port is taken as an option, so a misspelt port name sets nothing and says nothing.
    for (const auto& element : elements)
    {
        if (element.type == nullptr)
            continue;

        for (const auto& [key, value] : element.options)
        {
            // Editor metadata and what every element takes.
            if (key == "x" || key == "y" || key == "oversampling")
                continue;
            if (std::find(element.type->options.begin(), element.type->options.end(), key) != element.type->options.end())
                continue;

            std::vector<std::string> known = element.type->options;
            for (const auto& port : element.type->ports)
                if (port.input && port.control)
                    known.push_back(port.symbol);

            add(Severity::warning, "unknown-property", element.id, key,
                "val:" + key + " is set on " + shortId(element.id) + ", but val:" + shortId(element.typeIri)
                + " has no port or option of that name, so it does nothing."
                + didYouMean("val:", closestNames(key, known))
                + " describe_element names everything the element takes.");
        }
    }

    // 2. A knob bound to a port that an arc replaces.
    std::map<std::string, int> symbolCount;
    for (const auto& binding : model.params())
    {
        std::vector<std::pair<std::string, std::string>> targets{{binding.targetNode, binding.propertySymbol}};
        targets.insert(targets.end(), binding.alsoTargets.begin(), binding.alsoTargets.end());

        for (const auto& [node, property] : targets)
        {
            const auto reaching = controlled.find({node, property});
            if (reaching == controlled.end())
                continue;

            add(Severity::warning, "knob-overridden", node, property,
                "Parameter slot " + std::to_string(binding.slot) + " (" + binding.name + ") is bound to " + property
                + " on " + shortId(node) + ", but a control arc from " + shortId(reaching->second)
                + " replaces that value every block, so the knob does nothing. Bind it to the control source's "
                "resting value instead.");
        }

        if (! binding.symbol.empty())
            ++symbolCount[binding.symbol];
    }

    // 3. Two knobs with one symbol: a host sees them as the same parameter name.
    for (const auto& [symbol, count] : symbolCount)
        if (count > 1)
            add(Severity::warning, "duplicate-param-symbol", "", symbol,
                std::to_string(count) + " parameters share the symbol '" + symbol
                + "'. A host identifies automation by it, so give each its own.");

    // The graph of everything that feeds something else, for what can be heard.
    std::map<std::string, std::vector<std::string>> feeds;   // source -> destinations
    std::map<std::string, std::vector<std::string>> fedBy;   // destination -> sources
    std::set<std::string> audioIntoNode;
    std::map<std::string, std::set<std::string>> connectedAudioInputs;
    for (const auto& arc : arcs)
    {
        feeds[arc.fromNode].push_back(arc.toNode);
        fedBy[arc.toNode].push_back(arc.fromNode);
        if (! arc.control)
            connectedAudioInputs[arc.toNode].insert(arc.toPort);
    }

    // 4. Nothing reaches the output.
    const ElementInstance* output = nullptr;
    for (const auto& element : elements)
        if (element.type != nullptr && element.type->implementation == "Output")
            output = &element;

    if (output != nullptr && connectedAudioInputs[output->id].empty())
        add(Severity::error, "nothing-reaches-output", output->id, "",
            "Nothing is connected to the output, so the circuit is silent. Connect the last element's audio "
            "output to the output's 'in' (or 'left' and 'right').");

    // 5. Elements that cannot be heard: nothing they do reaches the output, by audio, control
    // or buffer arcs.
    if (output != nullptr && ! connectedAudioInputs[output->id].empty())
    {
        std::set<std::string> audible{output->id};
        std::vector<std::string> pending{output->id};
        while (! pending.empty())
        {
            const auto node = pending.back();
            pending.pop_back();
            for (const auto& source : fedBy[node])
                if (audible.insert(source).second)
                    pending.push_back(source);
        }

        for (const auto& element : elements)
        {
            if (element.type == nullptr || audible.count(element.id) != 0)
                continue;
            if (isDeadEndByDesign(element.type->implementation))
                continue;

            add(Severity::warning, "not-heard", element.id, "",
                shortId(element.id) + " (val:" + shortId(element.typeIri) + ") has no path to the output, so it cannot "
                "be heard. Connect it, or remove it.");
        }
    }

    // 6. An effect with nothing to process.
    for (const auto& element : elements)
    {
        if (element.type == nullptr || hasOptionalInputs(element.type->implementation))
            continue;

        const auto inputs = element.type->portsMatching(true, false);
        const auto outputs = element.type->portsMatching(false, false);
        if (inputs.empty() || outputs.empty())
            continue;

        if (connectedAudioInputs[element.id].empty())
            add(Severity::warning, "reads-silence", element.id, "",
                shortId(element.id) + " (val:" + shortId(element.typeIri) + ") has no audio connected to any input, so it "
                "processes silence.");
    }

    // 7. Said nothing about itself.
    if (! model.profile().declared)
        add(Severity::info, "no-profile", model.id(), "",
            "The circuit does not declare a trn:PluginProfile (a role, what it accepts and produces), so a catalogue "
            "cannot list it. Every circuit in examples/ declares one.");

    std::stable_sort(findings.begin(), findings.end(),
                     [](const LintFinding& a, const LintFinding& b) { return a.severity < b.severity; });
    return findings;
}

std::string lintToJson(const std::vector<LintFinding>& findings)
{
    int errors = 0, warnings = 0, infos = 0;
    for (const auto& f : findings)
    {
        errors   += f.severity == LintFinding::Severity::error;
        warnings += f.severity == LintFinding::Severity::warning;
        infos    += f.severity == LintFinding::Severity::info;
    }

    std::string json = "{\"errors\":" + std::to_string(errors) + ",\"warnings\":" + std::to_string(warnings)
                     + ",\"info\":" + std::to_string(infos) + ",\"findings\":[";
    for (std::size_t i = 0; i < findings.size(); ++i)
    {
        const auto& f = findings[i];
        const char* severity = f.severity == LintFinding::Severity::error ? "error"
                             : f.severity == LintFinding::Severity::warning ? "warning" : "info";
        json += std::string(i ? "," : "") + "{\"severity\":" + quote(severity) + ",\"code\":" + quote(f.code)
              + ",\"node\":" + quote(f.node) + ",\"port\":" + quote(f.port) + ",\"message\":" + quote(f.message) + "}";
    }
    return json + "]}";
}

}  // namespace valis

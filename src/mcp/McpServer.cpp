// src/mcp/McpServer.cpp

#include "mcp/McpServer.h"

#include "valis/AgentResources.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>

namespace valis {

namespace {

constexpr const char* kProtocolVersion = "2024-11-05";
constexpr const char* kTurtleResourceUri = "valis://turtle";
constexpr const char* kGraphResourceUri = "valis://graph";
constexpr const char* kDiagnosticsResourceUri = "valis://diagnostics";
constexpr const char* kElementTypesResourceUri = "valis://element-types";
constexpr const char* kParamsResourceUri = "valis://params";

// A request that cannot be serviced within this window is a bug, not a slow op: what runs on the message thread
// is bounded work on in-memory structures. What can take many seconds does not run there: tools on sound files
// (isFileTool), render (it prepares its job there and runs it elsewhere) and the searches (runSearchTool, which work
// from a snapshot).
constexpr int kRequestTimeoutMs = 5000;

/// Bounded admission for work marshalled onto the message thread.
///
/// The message thread does not always have an event loop. In a host that is not
/// JUCE — Transmission, say — nothing pumps JUCE events, so JUCE's Linux default
/// of posting to the X11 event queue has nobody draining it, and it asserts once
/// 128 posted messages are unprocessed. Every request waiting on a reply holds
/// one, so a burst of concurrent callers filled that queue and took the process
/// down with it. Bounding what is in flight keeps JUCE's queue short and makes
/// excess callers wait here, where they cost nothing, instead of there.
class MessageThreadGate
{
public:
    /// Blocks until a slot is free. False if none frees within `timeoutMs`.
    bool acquire(int timeoutMs)
    {
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(timeoutMs);

        std::unique_lock lock(mutex);
        if (! condition.wait_until(lock, deadline, [this] { return inFlight < kMaxInFlight; }))
            return false;

        ++inFlight;
        return true;
    }

    /// Called by the task itself, so a slot is held for as long as the work is
    /// queued rather than for as long as a caller is willing to wait.
    void release()
    {
        {
            const std::lock_guard lock(mutex);
            --inFlight;
        }

        condition.notify_one();
    }

    int pending() const
    {
        const std::lock_guard lock(mutex);
        return inFlight;
    }

private:
    /// Well under JUCE's 128, which also carries Valis's own timer messages.
    static constexpr int kMaxInFlight = 32;

    mutable std::mutex mutex;
    std::condition_variable condition;
    int inFlight = 0;
};

/// What a queued task writes into. Held by shared_ptr because a task the
/// message thread has not reached yet outlives the frame that posted it, which
/// is exactly the case where a caller gives up waiting.
template <typename Result>
struct PendingTask
{
    juce::WaitableEvent done;
    Result result{};
    juce::String error;
};

/// Runs `work` on the message thread and waits for it. The ops mutate the model
/// the editor also reads, so they belong on one thread; the HTTP thread waits
/// rather than taking a lock the editor would have to honour.
///
/// False means the request was not serviced and `error` says why. That is a
/// failure to report rather than a result to invent: a caller that gave up
/// waiting used to be handed a default-constructed result, which reads as a
/// successful empty answer.
template <typename Work>
auto onMessageThread(const std::shared_ptr<MessageThreadGate>& gate, Work&& work,
                     juce::String& error)
    -> std::optional<std::invoke_result_t<Work&, juce::String&>>
{
    using Result = std::invoke_result_t<Work&, juce::String&>;

    // Already there: run it here and take its error, since the caller's own
    // `error` is what it will read either way.
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        error = juce::String();
        return work(error);
    }

    if (! gate->acquire(kRequestTimeoutMs))
    {
        error = juce::String("the message thread is not servicing requests: ")
              + juce::String(gate->pending()) + " already in flight. A host that does not "
              "pump JUCE messages will hold every request here.";
        return std::nullopt;
    }

    auto task = std::make_shared<PendingTask<Result>>();

    juce::MessageManager::callAsync([gate, task, work = std::forward<Work>(work)]() mutable
    {
        task->result = work(task->error);
        gate->release();
        task->done.signal();
    });

    if (! task->done.wait(kRequestTimeoutMs))
    {
        // The task keeps the state it needs, so abandoning it here is safe.
        error = "the message thread did not service this request within "
              + juce::String(kRequestTimeoutMs) + "ms";
        return std::nullopt;
    }

    error = task->error;
    return task->result;
}

/// A named tool argument, as the ops take it. Free rather than a local lambda so
/// a queued task can hold the arguments by value instead of referencing them.
std::string stringArgument(const juce::var& arguments, const char* key)
{
    return arguments[key].toString().toStdString();
}

juce::var errorObject(int code, const juce::String& message)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("code", code);
    object->setProperty("message", message);
    return juce::var(object);
}

juce::var textContent(const juce::String& text, bool isError = false)
{
    auto* item = new juce::DynamicObject();
    item->setProperty("type", "text");
    item->setProperty("text", text);

    juce::Array<juce::var> content;
    content.add(juce::var(item));

    auto* result = new juce::DynamicObject();
    result->setProperty("content", content);
    if (isError)
        result->setProperty("isError", true);

    return juce::var(result);
}

juce::var textResource(const char* uri, const char* name, const char* description,
                       const char* mimeType)
{
    auto* resource = new juce::DynamicObject();
    resource->setProperty("uri", uri);
    resource->setProperty("name", name);
    resource->setProperty("description", description);
    resource->setProperty("mimeType", mimeType);
    return juce::var(resource);
}

juce::var resourceManifest()
{
    juce::Array<juce::var> resources;
    resources.add(textResource(kTurtleResourceUri, "Current Turtle",
                               "The circuit's current Turtle source.",
                               "text/turtle"));
    resources.add(textResource(kGraphResourceUri, "Current graph",
                               "The current circuit as JSON.",
                               "application/json"));
    resources.add(textResource(kDiagnosticsResourceUri, "Current diagnostics",
                               "Whether the circuit loaded, its size, and its latency.",
                               "application/json"));
    resources.add(textResource(kElementTypesResourceUri, "Element types",
                               "Every element class the ontology declares, with ports, ranges, and units.",
                               "application/json"));
    resources.add(textResource(kParamsResourceUri, "Parameter bindings",
                               "The circuit's parameter bindings, with ranges, units, and current values.",
                               "application/json"));

    // The guide and the examples: files, not state, so they need no thread of their own.
    for (const auto& item : listAgentResources(VALIS_ROOT_DIR, VALIS_EXAMPLES_DIR))
    {
        auto* resource = new juce::DynamicObject();
        resource->setProperty("uri", juce::String(item.uri));
        resource->setProperty("name", juce::String(item.name));
        resource->setProperty("description", juce::String(item.description));
        resource->setProperty("mimeType", juce::String(item.mimeType));
        resources.add(juce::var(resource));
    }
    return resources;
}

juce::String diagnosticsToText(const std::vector<Diagnostic>& diagnostics)
{
    juce::StringArray lines;
    for (const auto& d : diagnostics)
        lines.add(juce::String(d.toString()));

    return lines.joinIntoString("\n");
}

juce::var resultOf(const OpResult& result)
{
    if (! result.ok)
        return textContent(result.diagnostics.empty()
                               ? juce::String("the operation failed")
                               : diagnosticsToText(result.diagnostics),
                           true);

    juce::String text(result.value);
    if (! result.diagnostics.empty())
        text += (text.isEmpty() ? "" : "\n") + diagnosticsToText(result.diagnostics);

    return textContent(text.isEmpty() ? juce::String("ok") : text);
}

juce::var readResource(std::function<OpDispatcher()> makeDispatcher,
                       const std::shared_ptr<MessageThreadGate>& gate,
                       const juce::String& uri, juce::String& error)
{
    // The guide and the examples are plain files and are answered here.
    if (uri.startsWith("valis://guide") || uri.startsWith("valis://examples"))
    {
        const auto text = readAgentResource(VALIS_ROOT_DIR, VALIS_EXAMPLES_DIR, uri.toStdString());
        if (! text)
        {
            error = "no such resource: " + uri;
            return {};
        }

        const bool turtle = uri.startsWith("valis://examples/");
        auto* contents = new juce::DynamicObject();
        contents->setProperty("uri", uri);
        contents->setProperty("mimeType", turtle ? "text/turtle" : (uri == "valis://guide" ? "text/markdown" : "text/plain"));
        contents->setProperty("text", juce::String(*text));

        juce::Array<juce::var> items;
        items.add(juce::var(contents));

        auto* response = new juce::DynamicObject();
        response->setProperty("contents", items);
        return juce::var(response);
    }

    return onMessageThread(gate, [makeDispatcher, uri](juce::String& taskError) -> juce::var
    {
        auto ops = makeDispatcher();

        if (uri == kTurtleResourceUri)
        {
            const auto result = ops.getTurtle();
            if (! result.ok)
            {
                taskError = result.diagnostics.empty() ? "failed to read Turtle" : diagnosticsToText(result.diagnostics);
                return {};
            }

            auto* contents = new juce::DynamicObject();
            contents->setProperty("uri", uri);
            contents->setProperty("mimeType", "text/turtle");
            contents->setProperty("text", juce::String(result.value));

            juce::Array<juce::var> items;
            items.add(juce::var(contents));

            auto* response = new juce::DynamicObject();
            response->setProperty("contents", items);
            return juce::var(response);
        }

        if (uri == kGraphResourceUri || uri == kDiagnosticsResourceUri)
        {
            const auto result = uri == kGraphResourceUri ? ops.getGraph() : ops.getDiagnostics();
            if (! result.ok)
            {
                taskError = result.diagnostics.empty() ? "failed to read resource" : diagnosticsToText(result.diagnostics);
                return {};
            }

            auto* contents = new juce::DynamicObject();
            contents->setProperty("uri", uri);
            contents->setProperty("mimeType", "application/json");
            contents->setProperty("text", juce::String(result.value));

            juce::Array<juce::var> items;
            items.add(juce::var(contents));

            auto* response = new juce::DynamicObject();
            response->setProperty("contents", items);
            return juce::var(response);
        }

        if (uri == kElementTypesResourceUri)
        {
            juce::Array<juce::var> types;
            for (const auto& type : ops.listElementTypes())
            {
                auto* entry = new juce::DynamicObject();
                entry->setProperty("class", juce::String(type.classIri));
                entry->setProperty("label", juce::String(type.label));
                entry->setProperty("linear", type.linear);

                juce::Array<juce::var> ports;
                for (const auto& portDesc : type.ports)
                {
                    auto* p = new juce::DynamicObject();
                    p->setProperty("symbol", juce::String(portDesc.symbol));
                    p->setProperty("name", juce::String(portDesc.name));
                    p->setProperty("direction", portDesc.input ? "in" : "out");
                    p->setProperty("rate", portDesc.rateName());
                    if (portDesc.control)
                    {
                        p->setProperty("default", portDesc.defaultValue);
                        p->setProperty("minimum", portDesc.minimum);
                        p->setProperty("maximum", portDesc.maximum);
                        if (! portDesc.unit.empty())
                            p->setProperty("unit", juce::String(portDesc.unit));
                    }
                    ports.add(juce::var(p));
                }
                entry->setProperty("ports", ports);
                types.add(juce::var(entry));
            }

            auto* contents = new juce::DynamicObject();
            contents->setProperty("uri", uri);
            contents->setProperty("mimeType", "application/json");
            contents->setProperty("text", juce::JSON::toString(types, true));

            juce::Array<juce::var> items;
            items.add(juce::var(contents));

            auto* response = new juce::DynamicObject();
            response->setProperty("contents", items);
            return juce::var(response);
        }

        if (uri == kParamsResourceUri)
        {
            juce::Array<juce::var> params;
            for (const auto& param : ops.listParams())
            {
                auto* entry = new juce::DynamicObject();
                entry->setProperty("slot", param.slot);
                entry->setProperty("name", juce::String(param.name));
                entry->setProperty("target", juce::String(param.targetNode));
                entry->setProperty("property", juce::String(param.property));
                entry->setProperty("value", param.value);
                entry->setProperty("minimum", param.minimum);
                entry->setProperty("maximum", param.maximum);
                if (! param.unit.empty())
                    entry->setProperty("unit", juce::String(param.unit));
                params.add(juce::var(entry));
            }

            auto* contents = new juce::DynamicObject();
            contents->setProperty("uri", uri);
            contents->setProperty("mimeType", "application/json");
            contents->setProperty("text", juce::JSON::toString(params, true));

            juce::Array<juce::var> items;
            items.add(juce::var(contents));

            auto* response = new juce::DynamicObject();
            response->setProperty("contents", items);
            return juce::var(response);
        }

        taskError = "unknown resource: " + uri;
        return {};
    }, error).value_or(juce::var{});
}

/// A tool definition, so the manifest and the dispatch stay in one place.
struct ToolSpec
{
    const char* name;
    const char* description;
    const char* schema;   ///< the inputSchema, verbatim JSON
};

const ToolSpec kTools[] = {
    {"get_turtle", "Return the circuit's Turtle source.",
     R"({"type":"object","properties":{}})"},

    {"set_turtle", "Replace the circuit. Rejected if it will not compile; the previous circuit keeps playing.",
     R"({"type":"object","properties":{"turtle":{"type":"string","description":"the complete Turtle document"}},"required":["turtle"]})"},

    {"validate", "Check Turtle without installing it. Reports every problem found, with line and column where the parser found it.",
     R"({"type":"object","properties":{"turtle":{"type":"string"}},"required":["turtle"]})"},

    {"list_element_types", "Every element class the ontology declares, with its ports, ranges and units.",
     R"({"type":"object","properties":{}})"},

    {"check_stability", "Run the circuit on a fresh engine with random settings of its own parameters, every one of them jumping partway through, and report any run that goes non-finite, blows up (peak above +40 dBFS), clips or falls silent. The first run uses the declared settings and the rest are repeatable. Settings are returned for any run that is not fine. What is playing is not touched. Use it before saying a circuit is finished; bind the controls a player would reach with val:Param first, since only bound parameters are varied.",
     R"({"type":"object","properties":{"trials":{"type":"integer","description":"1 to 32, default 8"},"seconds":{"type":"number","description":"0.25 to 10, default 1.5"},"note":{"type":"integer","description":"MIDI note to hold, 0-127; -1 for none; default 60"}}})"},

    {"history", "The versions the circuit has had, oldest first: a number, what made each, and which is current. Every change made through these tools adds one; a change made in the editor is kept as a version the next time one of these is used. Up to 100 are kept.",
     R"({"type":"object","properties":{}})"},

    {"undo", "Take back the last change. The circuit goes back to the version before this one, and redo can bring the change back. If nothing was recorded before this (a circuit just loaded) there is nothing to undo.",
     R"({"type":"object","properties":{}})"},

    {"redo", "Bring back a change that undo took back. A new change after an undo ends the redo.",
     R"({"type":"object","properties":{}})"},

    {"diff", "What changed between two versions, in plain sentences: elements added and removed, values and options set, arcs made and broken, parameters bound. With no arguments, the version before the current one against the current one, which is what the last change did. from and to are version numbers from history.",
     R"({"type":"object","properties":{"from":{"type":"integer"},"to":{"type":"integer"}}})"},

    {"apply_edits", "Several edits as one: add_node, remove_node, connect and disconnect, in order. All of them take effect, or none do. They are applied to a private copy first, so a later edit can use a node an earlier one added, and the circuit is installed once as a whole, so what is playing never passes through a half-built state. The first edit that fails stops the batch and is named by its position. Ids without a colon are in the circuit's own namespace, so \"osc\" works for urn:valis:new#osc. Use it to build or restructure a circuit in one call instead of many.",
     R"({"type":"object","properties":{"edits":{"type":"array","maxItems":500,"items":{"type":"object","properties":{"op":{"type":"string","enum":["add_node","remove_node","connect","disconnect"]},"id":{"type":"string","description":"add_node, remove_node"},"class":{"type":"string","description":"add_node, e.g. val:Ladder"},"from_node":{"type":"string"},"from_port":{"type":"string"},"to_node":{"type":"string"},"to_port":{"type":"string"},"depth":{"type":"number","description":"connect, control arcs"}},"required":["op"]}}},"required":["edits"]})"},

    {"describe_element", "Everything about one element class in one call: what it is for and how to use it, every port with its range, unit, choices and description, and the options it takes besides its ports (a file, a pattern, a buffer length) which no port list shows. Use it before using an element you do not know. The name may be val:Ladder, Ladder, or any capitalisation; an unknown name returns the nearest real ones.",
     R"({"type":"object","properties":{"class":{"type":"string","description":"e.g. val:StepSequencer"}},"required":["class"]})"},

    {"get_graph", "The circuit as JSON: elements, their properties, and the arcs between them.",
     R"({"type":"object","properties":{}})"},

    {"add_node", "Add an element to the circuit.",
     R"({"type":"object","properties":{"id":{"type":"string","description":"IRI for the new element"},"class":{"type":"string","description":"element class, e.g. val:Ladder"}},"required":["id","class"]})"},

    {"remove_node", "Remove an element and every arc that touches it.",
     R"({"type":"object","properties":{"id":{"type":"string"}},"required":["id"]})"},

    {"connect", "Join an output port to an input port.",
     R"({"type":"object","properties":{"from_node":{"type":"string"},"from_port":{"type":"string"},"to_node":{"type":"string"},"to_port":{"type":"string"},"depth":{"type":"number","description":"modulation depth, control arcs only"}},"required":["from_node","from_port","to_node","to_port"]})"},

    {"disconnect", "Remove an arc between two ports.",
     R"({"type":"object","properties":{"from_node":{"type":"string"},"from_port":{"type":"string"},"to_node":{"type":"string"},"to_port":{"type":"string"}},"required":["from_node","from_port","to_node","to_port"]})"},

    {"list_params", "The circuit's parameter bindings, with their ranges, units and current values.",
     R"({"type":"object","properties":{}})"},

    {"get_param", "Read one parameter slot, in the property's own units.",
     R"({"type":"object","properties":{"slot":{"type":"integer"}},"required":["slot"]})"},

    {"set_param", "Set one parameter slot, in the property's own units. Out-of-range values are clamped and reported.",
     R"({"type":"object","properties":{"slot":{"type":"integer"},"value":{"type":"number"}},"required":["slot","value"]})"},

    {"morph", "Move every parameter that declares val:valueA and val:valueB to its place between them: 0 is all A, 1 is all B. A macro across the circuit.",
     R"({"type":"object","properties":{"position":{"type":"number","description":"0 to 1"}},"required":["position"]})"},

    {"randomize", "Give every parameter a new random value within its range, from a seed: the same seed on the same circuit gives the same patch. amount (0 to 1, default 1) is how far each moves toward its random value. keep lists slots to leave alone.",
     R"({"type":"object","properties":{"seed":{"type":"integer"},"amount":{"type":"number"},"keep":{"type":"array","items":{"type":"integer"}}},"required":["seed"]})"},

    {"get_sample", "The file a val:SampleLoad or val:TimeStretch (sound) or val:NamModel (.nam model) node is using.",
     R"({"type":"object","properties":{"node":{"type":"string"}},"required":["node"]})"},

    {"set_sample", "Load a different file into a val:SampleLoad or val:TimeStretch (sound) or val:NamModel (.nam model) node. The previous file stays in use if the new one will not load.",
     R"({"type":"object","properties":{"node":{"type":"string"},"path":{"type":"string","description":"absolute, or relative to the working directory or examples/"}},"required":["node","path"]})"},

    {"get_diagnostics", "The circuit's current state: whether it loaded, its size, its latency, and any problems an element recovered from (a missing sound file, say) as `problems`.",
     R"({"type":"object","properties":{}})"},

    {"load_file", "Load a Turtle circuit from a file path on the server machine.",
     R"({"type":"object","properties":{"path":{"type":"string","description":"absolute path to a .ttl file"}},"required":["path"]})"},

    {"get_profile", "What the circuit says it is: its role, the signals it deals in, what it pairs with, and what to watch out for. The terms are the transmissions vocabulary a plugin catalogue already uses.",
     R"({"type":"object","properties":{}})"},

    {"save_file", "Write the current circuit to a Turtle file. What the editor's Save does.",
     R"({"type":"object","properties":{"path":{"type":"string","description":"absolute, or relative to the working directory"}},"required":["path"]})"},

    {"note_on", "Play a note, as the editor's virtual keyboard does. The circuit keeps playing until note_off.",
     R"({"type":"object","properties":{"note":{"type":"integer","description":"MIDI note number, 0-127"},"velocity":{"type":"number","description":"0-1, default 1"}},"required":["note"]})"},

    {"note_off", "Release a note previously played with note_on.",
     R"({"type":"object","properties":{"note":{"type":"integer","description":"MIDI note number, 0-127"}},"required":["note"]})"},

    {"all_notes_off", "Release every held note.",
     R"({"type":"object","properties":{}})"},

    {"read_outputs", "The live value of every control output in the circuit, or of one node's. This is what the Controls view shows for val:Oscilloscope and val:FreqAnalyzer, and it is how you measure what a circuit is doing.",
     R"({"type":"object","properties":{"node":{"type":"string","description":"optional; omit for every node that has control outputs"}}})"},

    {"lint", "Static checks on the loaded circuit, for things that compile and still do not do what was meant: a fixed value on a port a control arc overwrites, a knob that moves nothing, an arc between an audio port and a control port (the compiler drops it), an element with no path to the output, an effect with nothing connected to process, nothing wired to the output. Each finding has a severity, a code, the node and port, and what to do. Run it after building, before render.",
     R"({"type":"object","properties":{}})"},

    {"measure", "The full measurement of a sound file, in place of listening: the signal analysis (peak, RMS, clipping, DC, the pitch it sits on with note name and cents, the level of each octave, brightness, where notes begin, level over time, plain notes on anything wrong), plus loudness to BS.1770 (integrated LUFS, loudness range, short-term and momentary maxima, true peak in dBTP) and the envelope (when the attack starts and peaks, attack time, time to fall 30 dB, noise floor, dynamic range). Read signal.notes first, then signal.pitch, loudness and envelope. These are measured numbers, not a judgement that it sounds good.",
     R"({"type":"object","properties":{"path":{"type":"string","description":"a wav, aiff, flac, ogg or mp3 file; the first ten minutes are read"}},"required":["path"]})"},

    {"compare", "How far apart two renders are: the spectral distance in dB (0 is the same sound, a few dB a small change, tens of dB a different sound), the distance at each of three resolutions, the difference in integrated loudness and in length. Use it after changing a circuit to see whether the change did anything. Both files must have the same sample rate.",
     R"({"type":"object","properties":{"path_a":{"type":"string","description":"the reference, a wav, aiff, flac, ogg or mp3 file"},"path_b":{"type":"string","description":"the file to compare with it; the differences are b minus a"}},"required":["path_a","path_b"]})"},

    {"listen", "Ask neural models what a sound is. tags are the most likely of 527 AudioSet labels (AST) for the loudest ten seconds; matches score each sentence in queries against the sound (CLAP): a cosine and a share, where the shares of the sentences given add up to 1. Read the share, and compare sentences with each other, since a single cosine means little. Give two or three competing descriptions, such as the instrument you want and the one it might be mistaken for. Needs a build with VALIS_WITH_ONNX and models fetched by scripts/shot/fetch-models.sh; otherwise it says so. The first call loads the models, which takes a few seconds. A neural model is a rough ear: this is measured, not heard.",
     R"({"type":"object","properties":{"path":{"type":"string","description":"a wav, aiff, flac, ogg or mp3 file"},"queries":{"type":"array","items":{"type":"string"},"description":"sentences describing candidate sounds, e.g. 'a plucked nylon string guitar'; omit for tags only"}},"required":["path"]})"},

    {"sounds_like", "The simple way to ask whether a sound is what you meant. Give a file and one sentence, such as 'a trumpet'; the answer is yes, maybe or no, the sentence's share, its rank among twenty common rivals (other instruments, a synthesizer preset, noise, a sine) and the three sentences the sound is closest to. 'no' with 'a synthesizer keyboard preset' closest is how a synthetic-sounding instrument shows up. Use listen instead to compare sentences of your own. Needs a build with VALIS_WITH_ONNX and models from scripts/shot/fetch-models.sh; otherwise it says so. A neural model is a rough ear: this is measured, not heard.",
     R"({"type":"object","properties":{"path":{"type":"string","description":"a wav, aiff, flac, ogg or mp3 file"},"sentence":{"type":"string","description":"what it should sound like, e.g. 'a plucked nylon string guitar'"}},"required":["path","sentence"]})"},

    {"fit_modes", "Turn a recording of a struck object (a bell, a cowbell, a conga, a wood block) into the modes of a val:FreeModalBank: up to 64 damped sines, each with a frequency, a ring time (the time to fall 60 dB) and a starting level. Returns them as a val:modes string and a Turtle line to paste into a circuit, with the distance in dB between the recording and the bank struck by an impulse. Wire a short noise burst or an impulse into the bank to play it, and val:tune to play it at other pitches. The recording should be one struck sound without a held tone; it matches the spectrum and ring times, not the attack. Measured, not heard.",
     R"({"type":"object","properties":{"path":{"type":"string","description":"the recording"},"modes":{"type":"integer","description":"most modes to find, 1 to 64, default 12"},"seconds":{"type":"number","description":"how much of the recording to read, 0.25 to 10, default 2"}},"required":["path"]})"},

    {"fit_params", "Search the circuit's own parameters for the settings that sound nearest a recording. Plays one note on a fresh engine, scores each render against the recording by spectral distance in dB, and moves the parameters by a seeded search, so the same call gives the same answer. With 24 evaluations or more it first tries a spread of starting points and climbs from the best, so one poor start does not trap it. The note is released at 80% of a whole recording, and not inside the window when only the start of a longer recording is matched; give hold (seconds) if you know it. Reports the distance before and after and the value found for each parameter, and says so if the best match is still far off; it changes nothing, so apply the values with set_param or in the Turtle. Needs val:Param bindings. A render takes a fraction of a second, so 60 evaluations is a few seconds to a minute. The distance is measured, not heard: a small one means the spectra agree.",
     R"({"type":"object","properties":{"path":{"type":"string","description":"the recording to match"},"note":{"type":"integer","description":"MIDI note to play, 0-127"},"velocity":{"type":"number","description":"0-1, default 1"},"seconds":{"type":"number","description":"how much of the recording to match, 0.25 to 10, default 3"},"evaluations":{"type":"integer","description":"renders to spend, 2 to 400, default 60"},"slots":{"type":"array","items":{"type":"integer"},"description":"parameter slots to search; omit to search all"},"hold":{"type":"number","description":"seconds the note was held in the recording; omit to release at 80% of a whole recording, and not within the window when it is only the start of a longer one"}},"required":["path","note"]})"},

    {"render", "Render the current circuit offline to a wav file and report its peak and RMS, and the same analysis measure gives under `signal`. Uses a fresh engine, so it neither hears nor disturbs what the plugin is playing. This is how you check that a circuit you built actually sounds.",
     R"({"type":"object","properties":{"path":{"type":"string","description":"output wav path"},"seconds":{"type":"number","description":"0.05 to 300, default 2"},"sample_rate":{"type":"number","description":"default 48000"},"note":{"type":"integer","description":"MIDI note to play, 0-127; omit for no note"},"velocity":{"type":"number","description":"0-1, default 1"},"midi":{"type":"string","description":"a standard MIDI file whose notes are played in place of note"},"tempo":{"type":"number","description":"beats per minute for the MIDI file and the transport, 20 to 400, default 120; tempo events in the file are ignored"},"rolling":{"type":"boolean","description":"report the transport as playing from zero, for circuits that take timing from the host"}},"required":["path"]})"},
};

}  // namespace

struct McpServer::Gate : MessageThreadGate
{
};

struct McpServer::Impl
{
    // A server that fails to bind cannot be reused (httplib decommissions it), so each attempt
    // makes a new one and the one that binds is kept here.
    std::unique_ptr<httplib::Server> server;
    std::thread thread;
    std::shared_ptr<Gate> gate = std::make_shared<Gate>();
};

McpServer::McpServer(std::function<OpDispatcher()> factory)
    : makeDispatcher(std::move(factory)), impl(std::make_unique<Impl>())
{
}

McpServer::~McpServer() { stop(); }

juce::var McpServer::toolManifest()
{
    juce::Array<juce::var> tools;

    for (const auto& spec : kTools)
    {
        auto* tool = new juce::DynamicObject();
        tool->setProperty("name", spec.name);
        tool->setProperty("description", spec.description);
        tool->setProperty("inputSchema", juce::JSON::parse(spec.schema));
        tools.add(juce::var(tool));
    }

    return tools;
}

namespace {

/// Tools that read a sound file and never the circuit. They can take seconds (the neural models alone take longer
/// than the message-thread window to load), so they run on the server's own thread: the editor and every other
/// call stay responsive, and a slow one cannot starve them. They share one set of neural models, hence the lock.
bool isFileTool(const juce::String& name)
{
    return name == "measure" || name == "compare" || name == "sounds_like" || name == "listen" || name == "fit_modes";
}

juce::var runFileTool(const OpDispatcher& ops, const juce::String& name, const juce::var& arguments)
{
    if (name == "measure")
        return resultOf(ops.measure(stringArgument(arguments, "path")));

    if (name == "compare")
        return resultOf(ops.compare(stringArgument(arguments, "path_a"), stringArgument(arguments, "path_b")));

    if (name == "sounds_like")
        return resultOf(ops.soundsLike(stringArgument(arguments, "path"), stringArgument(arguments, "sentence")));

    if (name == "listen")
    {
        std::vector<std::string> queries;
        if (const auto* list = arguments["queries"].getArray())
            for (const auto& q : *list)
                queries.push_back(q.toString().toStdString());
        return resultOf(ops.listen(stringArgument(arguments, "path"), queries));
    }

    if (name == "fit_modes")
        return resultOf(ops.fitModes(
            stringArgument(arguments, "path"),
            arguments.hasProperty("modes") ? static_cast<int>(arguments["modes"]) : 12,
            arguments.hasProperty("seconds") ? static_cast<double>(arguments["seconds"]) : 2.0));

    return textContent("unknown file tool: " + name, true);
}

std::mutex fileToolMutex;

/// The live circuit's Turtle and parameters, read on the message thread so the searches that follow can run elsewhere.
std::optional<CircuitSnapshot> takeSnapshot(const std::shared_ptr<MessageThreadGate>& gate,
                                            const std::function<OpDispatcher()>& factory, juce::String& error)
{
    const auto holder = std::make_shared<CircuitSnapshot>();
    const auto done = onMessageThread(gate, [factory, holder](juce::String&) -> int
    {
        *holder = factory().snapshot();
        return 0;
    }, error);

    if (! done.has_value())
        return std::nullopt;
    return *holder;
}

/// check_stability and fit_params build a fresh circuit for every trial or candidate, which can be hundreds of
/// renders. They run on the server's own thread from a snapshot, so the editor stays alive and a slow one cannot
/// block the next call. Everything they parse and compile is a private copy; see TurtleStore for why that is safe.
juce::var runSearchTool(const OpDispatcher& ops, const CircuitSnapshot& snapshot, const juce::String& name,
                        const juce::var& arguments)
{
    if (name == "check_stability")
    {
        const int trials = arguments.hasProperty("trials") ? static_cast<int>(arguments["trials"]) : 8;
        const double seconds = arguments.hasProperty("seconds") ? static_cast<double>(arguments["seconds"]) : 1.5;
        const int note = arguments.hasProperty("note") ? static_cast<int>(arguments["note"]) : 60;
        return resultOf(ops.checkStability(snapshot, trials, seconds, note));
    }

    std::vector<int> slots;
    if (const auto* list = arguments["slots"].getArray())
        for (const auto& v : *list)
            slots.push_back(static_cast<int>(v));
    return resultOf(ops.fitParams(
        snapshot,
        stringArgument(arguments, "path"),
        static_cast<int>(arguments["note"]),
        arguments.hasProperty("velocity") ? static_cast<double>(arguments["velocity"]) : 1.0,
        arguments.hasProperty("seconds") ? static_cast<double>(arguments["seconds"]) : 3.0,
        arguments.hasProperty("evaluations") ? static_cast<int>(arguments["evaluations"]) : 60,
        slots,
        arguments.hasProperty("hold") ? static_cast<double>(arguments["hold"]) : -1.0));
}

}  // namespace

juce::var McpServer::callTool(const std::shared_ptr<McpServer::Gate>& gate,
                              const juce::String& name, const juce::var& arguments,
                              juce::String& error)
{
    if (name == "render")
    {
        const double seconds = arguments.hasProperty("seconds") ? static_cast<double>(arguments["seconds"]) : 2.0;
        const double rate = arguments.hasProperty("sample_rate") ? static_cast<double>(arguments["sample_rate"]) : 48000.0;
        const int note = arguments.hasProperty("note") ? static_cast<int>(arguments["note"]) : -1;
        const double velocity = arguments.hasProperty("velocity") ? static_cast<double>(arguments["velocity"]) : 1.0;
        const double tempo = arguments.hasProperty("tempo") ? static_cast<double>(arguments["tempo"]) : 120.0;
        const bool rolling = arguments.hasProperty("rolling") && static_cast<bool>(arguments["rolling"]);
        const std::string path = stringArgument(arguments, "path");
        const std::string midi = arguments.hasProperty("midi") ? stringArgument(arguments, "midi") : std::string();

        // Reading the circuit (parse, build, compile, load) is the message thread's, as the rules say; the audio is
        // not, and for a long piece it is most of the time. The job is shared with the queued task, which may outlive
        // this frame if the message thread is late.
        const auto job = std::make_shared<std::unique_ptr<RenderJob>>();
        const auto factory = makeDispatcher;

        const auto prepared = onMessageThread(gate, [factory, job, path, seconds, rate, note, velocity, midi, tempo,
                                                     rolling](juce::String&) -> juce::var
        {
            OpResult failure;
            *job = factory().beginRender(path, seconds, rate, note, velocity, midi, tempo, rolling, failure);
            return *job == nullptr ? resultOf(failure) : juce::var();
        }, error);

        if (! prepared.has_value())
            return {};                       // `error` says the message thread was late
        if (*job == nullptr)
            return *prepared;                // the circuit would not build, or an argument was refused
        return resultOf((*job)->run());
    }

    if (name == "check_stability" || name == "fit_params")
    {
        const auto snapshot = takeSnapshot(gate, makeDispatcher, error);
        if (! snapshot.has_value())
            return {};                       // `error` says the message thread was late
        return runSearchTool(makeDispatcher(), *snapshot, name, arguments);
    }

    if (isFileTool(name))
    {
        // ops() only gathers pointers and callbacks, so building one here touches no circuit state.
        const std::lock_guard lock(fileToolMutex);
        return runFileTool(makeDispatcher(), name, arguments);
    }

    // Copied into the task rather than referenced: a task the message thread
    // has not reached yet can outlive this frame.
    const auto factory = makeDispatcher;

    const auto result = onMessageThread(gate, [factory, name, arguments](juce::String& taskError) -> juce::var
    {
        auto ops = factory();

        if (name == "get_turtle")          return resultOf(ops.getTurtle());
        if (name == "set_turtle")          return resultOf(ops.setTurtle(stringArgument(arguments, "turtle")));
        if (name == "validate")            return resultOf(ops.validate(stringArgument(arguments, "turtle")));
        if (name == "get_graph")           return resultOf(ops.getGraph());
        if (name == "get_diagnostics")     return resultOf(ops.getDiagnostics());

        if (name == "add_node")
            return resultOf(ops.addNode(stringArgument(arguments, "id"), stringArgument(arguments, "class")));

        if (name == "remove_node")
            return resultOf(ops.removeNode(stringArgument(arguments, "id")));

        if (name == "connect")
        {
            std::optional<double> depth;
            if (arguments.hasProperty("depth"))
                depth = static_cast<double>(arguments["depth"]);

            return resultOf(ops.connect(stringArgument(arguments, "from_node"), stringArgument(arguments, "from_port"),
                                        stringArgument(arguments, "to_node"),   stringArgument(arguments, "to_port"), depth));
        }

        if (name == "disconnect")
            return resultOf(ops.disconnect(stringArgument(arguments, "from_node"), stringArgument(arguments, "from_port"),
                                           stringArgument(arguments, "to_node"),   stringArgument(arguments, "to_port")));

        if (name == "get_param")
            return resultOf(ops.getParam(static_cast<int>(arguments["slot"])));

        if (name == "set_param")
            return resultOf(ops.setParam(static_cast<int>(arguments["slot"]),
                                         static_cast<double>(arguments["value"])));

        if (name == "morph")
            return resultOf(ops.morph(static_cast<double>(arguments["position"])));

        if (name == "randomize")
        {
            const double amount = arguments.hasProperty("amount") ? static_cast<double>(arguments["amount"]) : 1.0;

            std::vector<int> keep;
            if (const auto* list = arguments["keep"].getArray())
                for (const auto& slot : *list)
                    keep.push_back(static_cast<int>(slot));

            return resultOf(ops.randomize(static_cast<unsigned>(static_cast<int>(arguments["seed"])), amount, keep));
        }

        if (name == "get_sample")
            return resultOf(ops.getSample(stringArgument(arguments, "node")));

        if (name == "set_sample")
            return resultOf(ops.setSample(stringArgument(arguments, "node"), stringArgument(arguments, "path")));

        if (name == "get_profile")
            return resultOf(ops.getProfile());

        if (name == "save_file")
            return resultOf(ops.saveFile(stringArgument(arguments, "path")));

        if (name == "note_on")
        {
            const double velocity = arguments.hasProperty("velocity")
                                  ? static_cast<double>(arguments["velocity"]) : 1.0;
            return resultOf(ops.noteOn(static_cast<int>(arguments["note"]), velocity));
        }

        if (name == "note_off")
            return resultOf(ops.noteOff(static_cast<int>(arguments["note"])));

        if (name == "all_notes_off")
            return resultOf(ops.allNotesOff());

        if (name == "read_outputs")
            return resultOf(ops.readOutputs(stringArgument(arguments, "node")));

        if (name == "history")
            return resultOf(ops.history());

        if (name == "undo")
            return resultOf(ops.undo());

        if (name == "redo")
            return resultOf(ops.redo());

        if (name == "diff")
        {
            std::optional<int> from, to;
            if (arguments.hasProperty("from")) from = static_cast<int>(arguments["from"]);
            if (arguments.hasProperty("to"))   to   = static_cast<int>(arguments["to"]);
            return resultOf(ops.diff(from, to));
        }

        if (name == "apply_edits")
        {
            std::vector<GraphEdit> edits;
            if (const auto* list = arguments["edits"].getArray())
            {
                for (const auto& item : *list)
                {
                    GraphEdit edit;
                    const auto op = item["op"].toString();
                    if (op == "add_node")         edit.kind = GraphEdit::Kind::addNode;
                    else if (op == "remove_node") edit.kind = GraphEdit::Kind::removeNode;
                    else if (op == "connect")     edit.kind = GraphEdit::Kind::connect;
                    else if (op == "disconnect")  edit.kind = GraphEdit::Kind::disconnect;
                    else
                        return textContent("edit " + juce::String(static_cast<int>(edits.size()) + 1)
                                           + " has op '" + op + "'; use add_node, remove_node, connect or disconnect. Nothing was changed.", true);

                    edit.id        = stringArgument(item, "id");
                    edit.classIri  = stringArgument(item, "class");
                    edit.fromNode  = stringArgument(item, "from_node");
                    edit.fromPort  = stringArgument(item, "from_port");
                    edit.toNode    = stringArgument(item, "to_node");
                    edit.toPort    = stringArgument(item, "to_port");
                    if (item.hasProperty("depth"))
                        edit.depth = static_cast<double>(item["depth"]);
                    edits.push_back(std::move(edit));
                }
            }
            return resultOf(ops.applyEdits(edits));
        }

        if (name == "describe_element")
            return resultOf(ops.describeElement(stringArgument(arguments, "class")));

        if (name == "lint")
            return resultOf(ops.lint());

        if (name == "load_file")
        {
            const juce::File f(juce::String(stringArgument(arguments, "path")));
            if (!f.existsAsFile())
                return textContent("file not found: " + juce::String(stringArgument(arguments, "path")), true);
            const auto content = f.loadFileAsString();
            return resultOf(ops.setTurtle(content.toStdString()));
        }

        if (name == "list_element_types")
        {
            juce::Array<juce::var> types;
            for (const auto& type : ops.listElementTypes())
            {
                auto* entry = new juce::DynamicObject();
                entry->setProperty("class", juce::String(type.classIri));
                entry->setProperty("label", juce::String(type.label));
                entry->setProperty("linear", type.linear);

                juce::Array<juce::var> ports;
                for (const auto& portDesc : type.ports)
                {
                    auto* p = new juce::DynamicObject();
                    p->setProperty("symbol", juce::String(portDesc.symbol));
                    p->setProperty("name", juce::String(portDesc.name));
                    p->setProperty("direction", portDesc.input ? "in" : "out");
                    p->setProperty("rate", portDesc.rateName());
                    if (portDesc.control)
                    {
                        p->setProperty("default", portDesc.defaultValue);
                        p->setProperty("minimum", portDesc.minimum);
                        p->setProperty("maximum", portDesc.maximum);
                        if (! portDesc.unit.empty())
                            p->setProperty("unit", juce::String(portDesc.unit));
                    }
                    ports.add(juce::var(p));
                }
                entry->setProperty("ports", ports);
                types.add(juce::var(entry));
            }
            return textContent(juce::JSON::toString(types, true));
        }

        if (name == "list_params")
        {
            juce::Array<juce::var> params;
            for (const auto& param : ops.listParams())
            {
                auto* entry = new juce::DynamicObject();
                entry->setProperty("slot", param.slot);
                entry->setProperty("name", juce::String(param.name));
                entry->setProperty("target", juce::String(param.targetNode));
                entry->setProperty("property", juce::String(param.property));
                entry->setProperty("value", param.value);
                entry->setProperty("minimum", param.minimum);
                entry->setProperty("maximum", param.maximum);
                if (! param.unit.empty())
                    entry->setProperty("unit", juce::String(param.unit));
                params.add(juce::var(entry));
            }
            return textContent(juce::JSON::toString(params, true));
        }

        taskError = "unknown tool: " + name;
        return {};
    }, error);

    return result.value_or(juce::var{});
}

std::string McpServer::handleMessage(const std::string& request)
{
    const auto parsed = juce::JSON::parse(juce::String(request));

    auto* response = new juce::DynamicObject();
    response->setProperty("jsonrpc", "2.0");

    if (! parsed.isObject())
    {
        response->setProperty("id", juce::var());
        response->setProperty("error", errorObject(-32700, "parse error"));
        return juce::JSON::toString(juce::var(response), true).toStdString();
    }

    const auto id = parsed["id"];
    const auto method = parsed["method"].toString();
    response->setProperty("id", id);

    if (method == "initialize")
    {
        auto* info = new juce::DynamicObject();
        info->setProperty("name", "valis");
        info->setProperty("version", "0.1.0");

        auto* tools = new juce::DynamicObject();
        auto* resources = new juce::DynamicObject();
        auto* capabilities = new juce::DynamicObject();
        capabilities->setProperty("tools", juce::var(tools));
        capabilities->setProperty("resources", juce::var(resources));

        auto* result = new juce::DynamicObject();
        result->setProperty("protocolVersion", kProtocolVersion);
        result->setProperty("capabilities", juce::var(capabilities));
        result->setProperty("serverInfo", juce::var(info));

        response->setProperty("result", juce::var(result));
    }
    else if (method == "tools/list")
    {
        auto* result = new juce::DynamicObject();
        result->setProperty("tools", toolManifest());
        response->setProperty("result", juce::var(result));
    }
    else if (method == "resources/list")
    {
        auto* result = new juce::DynamicObject();
        result->setProperty("resources", resourceManifest());
        response->setProperty("result", juce::var(result));
    }
    else if (method == "resources/read")
    {
        const auto params = parsed["params"];

        juce::String error;
        const auto result = readResource(makeDispatcher, impl->gate,
                                        params["uri"].toString(), error);

        if (error.isNotEmpty())
            response->setProperty("error", errorObject(-32602, error));
        else
            response->setProperty("result", result);
    }
    else if (method == "tools/call")
    {
        const auto params = parsed["params"];
        const auto name = params["name"].toString();

        juce::String error;
        const auto result = callTool(impl->gate, name, params["arguments"], error);

        if (error.isNotEmpty())
            response->setProperty("error", errorObject(-32602, error));
        else
            response->setProperty("result", result);
    }
    else if (method == "notifications/initialized")
    {
        // A notification carries no id and expects no response.
        return {};
    }
    else
    {
        response->setProperty("error", errorObject(-32601, "method not found: " + method));
    }

    return juce::JSON::toString(juce::var(response), true).toStdString();
}

void McpServer::setIdentity(std::function<std::string()> describe)
{
    const std::lock_guard<std::mutex> lock(identityMutex);
    identity = std::move(describe);
}

bool McpServer::start(int requestedPort, const std::string& bearerToken, int portsToTry)
{
    if (running.load(std::memory_order_acquire))
        return true;

    token = bearerToken;

    const auto configure = [this](httplib::Server& server)
    {
        server.Post("/mcp", [this](const httplib::Request& request, httplib::Response& response)
        {
            if (! token.empty())
            {
                const auto header = request.get_header_value("Authorization");
                if (header != "Bearer " + token)
                {
                    response.status = 401;
                    response.set_content(R"({"error":"unauthorized"})", "application/json");
                    return;
                }
            }

            const auto reply = handleMessage(request.body);
            if (reply.empty())
            {
                response.status = 202;   // a notification was accepted
                return;
            }

            response.set_content(reply, "application/json");
        });

        server.Get("/health", [this](const httplib::Request&, httplib::Response& response)
        {
            std::string name;
            {
                const std::lock_guard<std::mutex> lock(identityMutex);
                if (identity) name = identity();
            }

            auto* object = new juce::DynamicObject();
            object->setProperty("status", "ok");
            object->setProperty("server", "valis");
            object->setProperty("port", port);
            object->setProperty("name", juce::String(name));
            response.set_content(juce::JSON::toString(juce::var(object), true).toStdString(),
                                 "application/json");
        });

        // httplib's default options include SO_REUSEPORT, which lets a second server bind a port
        // the first one holds, and the kernel then hands each request to either. Two instances
        // would answer for each other without either noticing. Allow only a quick rebind after a
        // stop.
        server.set_socket_options([](socket_t socket)
        {
            int yes = 1;
           #ifdef _WIN32
            setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof(yes));
           #else
            setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
           #endif
        });
    };

    // Loopback only. This is an editing surface for the machine the plugin runs
    // on, not a service.
    for (int attempt = 0; attempt < std::max(1, portsToTry); ++attempt)
    {
        const int candidate = requestedPort + attempt;
        if (candidate > 65535)
            break;

        auto candidateServer = std::make_unique<httplib::Server>();
        configure(*candidateServer);

        if (candidateServer->bind_to_port("127.0.0.1", candidate))
        {
            port = candidate;
            impl->server = std::move(candidateServer);
            running.store(true, std::memory_order_release);
            impl->thread = std::thread([this] { impl->server->listen_after_bind(); });

            // stop() does nothing to a server that has not begun to listen, and the thread would
            // then listen for ever and never join. Return only once it is listening.
            for (int waited = 0; waited < 400 && ! impl->server->is_running(); ++waited)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return true;
        }
    }

    port = 0;
    return false;
}

void McpServer::stop()
{
    if (! running.exchange(false, std::memory_order_acq_rel))
        return;

    impl->server->stop();
    if (impl->thread.joinable())
        impl->thread.join();
    impl->server.reset();
}

}  // namespace valis

// include/valis/Ops.h
//
// The headless command surface. Every key operation the plugin can perform is
// one Op, so the three views and the HTTP MCP server are thin adapters over the
// same thing rather than parallel implementations. Nothing here knows about a
// UI, a host, or a socket.
//
// Message thread only.

#pragma once

#include "valis/CircuitModel.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace valis {

class Ontology;
class ValisEngine;
class ElementRegistry;

/// Every op returns one of these. `ok` says whether the operation happened;
/// `diagnostics` explains why not, and may also carry warnings on success.
struct OpResult
{
    bool ok = false;
    std::string value;                    ///< the op's payload, when it has one
    std::vector<Diagnostic> diagnostics;

    static OpResult failure(std::string message)
    {
        return {false, {}, {Diagnostic{std::move(message), {}}}};
    }

    static OpResult success(std::string value = {})
    {
        return {true, std::move(value), {}};
    }
};

/// Describes one port for listElementTypes and the knobs view.
struct PortInfo
{
    std::string symbol, name, unit;
    bool input = true, control = false;

    /// An atom:AtomPort carrying events rather than a signal. It has no buffer
    /// and no range, so a caller reading this list has to know not to treat it
    /// as one it can connect a signal to.
    bool event = false;

    /// A val:BufferPort: it hands material to another element and takes no signal.
    bool buffer = false;

    double defaultValue = 0.0, minimum = 0.0, maximum = 1.0;

    /// "audio", "control", "event" or "buffer", for the tools that list ports.
    const char* rateName() const
    {
        return event ? "event" : buffer ? "buffer" : control ? "control" : "audio";
    }
};

struct ElementTypeInfo
{
    std::string classIri, label, implementation;
    bool linear = true;
    std::vector<PortInfo> ports;
};

struct ParamInfo
{
    int slot = -1;
    std::string name, symbol, unit, targetNode, property;
    double minimum = 0.0, maximum = 1.0, value = 0.0;
    std::optional<double> morphA, morphB;   ///< val:valueA and val:valueB, if declared
};

/// One structural edit for OpDispatcher::applyEdits.
struct GraphEdit
{
    enum class Kind { addNode, removeNode, connect, disconnect };

    Kind kind = Kind::connect;
    std::string id, classIri;                       ///< addNode, removeNode
    std::string fromNode, fromPort, toNode, toPort; ///< connect, disconnect
    std::optional<double> depth;                    ///< connect, control arcs
};

/// Anything the ops need to reach. Injected rather than owned, so the same ops
/// serve the plugin, the CLI and the tests.
class CircuitHistory;

struct OpContext
{
    const Ontology* ontology = nullptr;
    ValisEngine* engine      = nullptr;
    const ElementRegistry* registry = nullptr;

    /// Reads and replaces the circuit's Turtle source. Supplied by whoever owns
    /// it - the plugin, or a test.
    std::function<std::string()> readTurtle;
    std::function<bool(const std::string&, std::vector<Diagnostic>&)> writeTurtle;

    /// The model matching the Turtle currently installed.
    std::function<const CircuitModel*()> readModel;

    /// The versions the circuit has had, lent by whoever owns the circuit. Null means no history is
    /// kept, and the history ops say so. Every Op that changes the circuit adds a version to it.
    CircuitHistory* history = nullptr;

    /// Reads and replaces the sound file a val:SampleLoad or val:TimeStretch node is
    /// playing, or the model a val:NamModel runs. The
    /// document declares one with val:file; this is the session's choice on top
    /// of it, exactly as a turned knob sits on top of a declared value.
    std::function<std::string(const std::string& nodeId)> readSample;
    std::function<bool(const std::string& nodeId, const std::string& path,
                       std::string& error)> writeSample;
};

/// The operations. Each is deliberately small and total: it either does the
/// thing or explains why it could not.
/// A render that has been parsed, built, compiled and loaded into a private engine, and now only has audio to make.
/// Preparing one belongs on the message thread; `run` touches no live state and may take as long as the audio does,
/// so it can go on any other thread. Made by `OpDispatcher::beginRender`.
class RenderJob
{
public:
    ~RenderJob();
    RenderJob(const RenderJob&) = delete;
    RenderJob& operator=(const RenderJob&) = delete;

    /// Renders, writes the wav and measures it. Once only.
    OpResult run();

private:
    friend class OpDispatcher;
    struct Impl;
    explicit RenderJob(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl;
};

/// What a long operation needs from the live circuit, taken once so the operation can run on another thread: the
/// Turtle source and the list of bound parameters, with the values they have now. Taken by `OpDispatcher::snapshot`
/// on the message thread; the operations that accept one read nothing live. They do read the ontology and the
/// element registry, which are never written once loaded, and the ontology's store takes a lock for every lookup.
struct CircuitSnapshot
{
    bool loaded = false;
    std::string turtle;
    std::vector<ParamInfo> params;
};

class OpDispatcher
{
public:
    explicit OpDispatcher(OpContext context) : ctx(std::move(context)) {}

    // -- circuit source ----------------------------------------------------
    OpResult getTurtle() const;
    OpResult setTurtle(const std::string& turtle);
    OpResult validate(const std::string& turtle) const;

    // -- introspection -----------------------------------------------------
    std::vector<ElementTypeInfo> listElementTypes() const;
    OpResult getGraph() const;                     ///< the circuit as JSON

    /// Everything about one element class in one call: what it is for and how to use it, in
    /// the ontology's own words; every port with its range, unit, choices and description; and
    /// the options it takes besides its ports (a file, a pattern), which no port list shows.
    /// `name` is val:Ladder, Ladder, a full IRI, or any capitalisation of the name. An unknown
    /// name fails with the nearest real ones.
    OpResult describeElement(const std::string& name) const;

    // -- graph editing -----------------------------------------------------
    OpResult addNode(const std::string& id, const std::string& classIri);
    OpResult removeNode(const std::string& id);
    OpResult connect(const std::string& fromNode, const std::string& fromPort,
                     const std::string& toNode,   const std::string& toPort,
                     std::optional<double> depth = std::nullopt);
    OpResult disconnect(const std::string& fromNode, const std::string& fromPort,
                        const std::string& toNode,   const std::string& toPort);

    /// Several of the edits above as one: all of them take effect, or none do. They are applied
    /// in order to a private copy, so a later edit can refer to a node an earlier one added, and
    /// the circuit is installed once, as a whole, so what plays never passes through a half-built
    /// state. The first edit that cannot be applied stops the batch and is named by its position,
    /// and the circuit is left as it was. A circuit that is wrong only as a whole (two arcs into
    /// one audio input, no output) is reported when the result is installed, not before.
    ///
    /// An id without a colon is taken to be in the circuit's own namespace, so "osc" means
    /// the same as "urn:valis:new#osc" in a circuit whose id is "urn:valis:new#main".
    OpResult applyEdits(const std::vector<GraphEdit>& edits);

    // -- history -------------------------------------------------------------
    /// The versions the circuit has had, oldest first, with what made each and which is current.
    OpResult history() const;

    /// Takes back the last change: the circuit goes back to the version before this one, and the
    /// version it left stays, to `redo`. If the circuit's text was changed somewhere that does not
    /// record (the editor), that state is kept as a version first, so nothing is lost.
    OpResult undo();
    OpResult redo();

    /// What changed between two versions, in plain sentences: elements added and removed, values
    /// and options set, arcs made and broken, parameters bound. By default the version before the
    /// current one against the current one. `from` and `to` are version numbers from `history`.
    OpResult diff(std::optional<int> from = std::nullopt, std::optional<int> to = std::nullopt) const;

    // -- samples -----------------------------------------------------------
    /// The sound file a node is playing, or a failure if it is not a node that
    /// plays one.
    OpResult getSample(const std::string& nodeId) const;
    OpResult setSample(const std::string& nodeId, const std::string& path);

    // -- parameters --------------------------------------------------------
    std::vector<ParamInfo> listParams() const;
    OpResult getParam(int slot) const;
    OpResult setParam(int slot, double value);

    /// Moves every parameter that declares both val:valueA and val:valueB to its
    /// place between them: 0 is every A, 1 is every B. The result lists the slots
    /// moved. A circuit with no morph bindings is a failure, not a silent no-op.
    OpResult morph(double position);

    /// Gives every parameter a new random value within its range, from a seed, so
    /// the same seed on the same circuit gives the same patch. `amount` is how far
    /// each moves from where it is toward its random value: 1 replaces it, 0.1
    /// nudges it. Parameters named in `keep` (by slot) are left alone. Surfaces
    /// the values it set.
    OpResult randomize(unsigned seed, double amount = 1.0, const std::vector<int>& keep = {});

    // -- playing -----------------------------------------------------------
    /// What the editor's virtual keyboard does. A caller driving Valis from
    /// outside needs this to hear what it has built.
    OpResult noteOn(int noteNumber, double velocity);
    OpResult noteOff(int noteNumber);
    OpResult allNotesOff();

    // -- measuring ---------------------------------------------------------
    /// Every control output the circuit currently carries, or just one node's.
    /// This is what the Controls view shows live for val:Oscilloscope and
    /// val:FreqAnalyzer, and it is how a caller measures what it built.
    OpResult readOutputs(const std::string& nodeId = {}) const;

    /// Runs the circuit on a fresh engine with random settings of its own parameters, every one of
    /// them jumping partway through, and reports any run that goes non-finite, blows up (a peak
    /// above +40 dBFS), clips or falls silent. `trials` runs, the first on the declared settings and
    /// the rest from seeds 1 and up, so a result can be repeated; each is `seconds` long, with a note
    /// held if `note` is not negative, and a noise-and-tone input for an effect to process. What is
    /// playing is not touched. For a run that is not ok the settings are returned.
    OpResult checkStability(int trials = 8, double seconds = 1.5, int note = 60) const;
    /// The same on a snapshot, which touches nothing live and may run on any thread.
    OpResult checkStability(const CircuitSnapshot& snapshot, int trials, double seconds, int note) const;

    /// Reads the live circuit for the operations that take a snapshot. Message thread.
    CircuitSnapshot snapshot() const;

    /// Searches the circuit's own parameters for the settings whose render is nearest a recording. Plays `note`
    /// on a fresh engine at the recording's sample rate for up to `seconds` of it, scores each render against the
    /// recording with the spectral distance in dB (include/valis/SpectralDistance.h), and moves the parameters
    /// by a seeded evolution strategy, so a result can be repeated. `evaluations` renders at most; `slots`
    /// limits the search to those parameters, and empty searches all. Reports the distance before and after and
    /// the value found for each parameter. It changes nothing: set the values with setParam or in the Turtle.
    /// The distance is measured, not heard; a small one means the spectra agree, not that the sound is right.
    ///
    /// `holdSeconds` is how long the note was held in the recording. Below zero (the default) the note is released
    /// at 80% of the recording when all of it is matched, and not at all inside the window when only the start of a
    /// longer recording is, since its release came later. With 24 renders or more the search first tries a seeded
    /// spread of starting points and climbs from the best two or three, so one poor start does not trap it; the
    /// result says how many it climbed from and, if the best match is still far off, why that may be.
    OpResult fitParams(const std::string& targetPath, int note, double velocity, double seconds,
                       int evaluations, const std::vector<int>& slots = {}, double holdSeconds = -1.0) const;
    /// The same on a snapshot, which touches nothing live and may run on any thread.
    OpResult fitParams(const CircuitSnapshot& snapshot, const std::string& targetPath, int note, double velocity,
                       double seconds, int evaluations, const std::vector<int>& slots, double holdSeconds = -1.0) const;

    /// Turns a recording of a struck object (a bell, a cowbell, a conga) into the modes of a val:FreeModalBank
    /// (include/valis/ModalFit.h): up to `modes` damped sines, each with a frequency, a ring time and a level.
    /// Reports them as a val:modes string and a Turtle line to paste, and how far the bank, struck by an
    /// impulse, is from the recording in dB of spectral distance. Reads up to `seconds` of the file. Measured,
    /// not heard: it matches the spectrum and ring times, not the attack or the way the sound is played.
    OpResult fitModes(const std::string& path, int modes = 12, double seconds = 2.0) const;

    /// Static checks on the loaded circuit: values a control arc overwrites, knobs that move
    /// nothing, parts that cannot be heard, effects with nothing to process, nothing wired to
    /// the output. See include/valis/Lint.h. A circuit that compiles can still earn findings.
    OpResult lint() const;

    /// The valis-shot measurements (include/valis/ShotReport.h), the same code the command line tool runs.
    /// `measure` is the signal analysis (level, clipping, DC, pitch, octave levels, brightness, onsets, plain notes) plus BS.1770 loudness and attack and decay times; `compare` is how far apart two
    /// renders are in dB; `listen` names the sound with neural models and scores sentences against it, and fails
    /// with the reason in a build without them or before scripts/shot/fetch-models.sh has been run.
    OpResult measure(const std::string& path) const;
    OpResult compare(const std::string& pathA, const std::string& pathB) const;
    OpResult listen(const std::string& path, const std::vector<std::string>& queries) const;
    /// The one-question form of listen: yes, maybe or no, and what the sound is closest to.
    OpResult soundsLike(const std::string& path, const std::string& sentence) const;

    /// Renders the circuit offline into a wav file and reports what came out.
    /// A fresh engine, so it neither touches nor is touched by whatever the
    /// plugin is playing. `note` below zero renders without a note event. A `midiPath` plays every note of that MIDI
    /// file at `tempo` in place of `note`; `rolling` reports the transport as playing from zero at that tempo.
    OpResult render(const std::string& path, double seconds, double sampleRate,
                    int note, double velocity,
                    const std::string& midiPath = {}, double tempo = 120.0, bool rolling = false) const;

    /// The first half of `render`, which is the part that reads the circuit: checks the arguments, then parses, builds
    /// and compiles the circuit into a private engine. Null means it failed, and `failure` says why. Run the result
    /// with `RenderJob::run`, on this thread or another: `render` is exactly the two in a row. `seconds` is held to
    /// 0.05 to 300.
    std::unique_ptr<RenderJob> beginRender(const std::string& path, double seconds, double sampleRate,
                                           int note, double velocity, const std::string& midiPath,
                                           double tempo, bool rolling, OpResult& failure) const;

    // -- files -------------------------------------------------------------
    /// Writes the current Turtle to a path. The editor's Save, for a caller
    /// that has none.
    OpResult saveFile(const std::string& path) const;

    // -- profile -----------------------------------------------------------
    /// What the circuit says it is: its role, the signals it deals in, what it
    /// pairs with. The terms are the transmissions vocabulary a plugin
    /// catalogue already uses, so a listing needs no translation.
    OpResult getProfile() const;

    // -- diagnostics -------------------------------------------------------
    OpResult getDiagnostics() const;

private:
    // The edits above, before a version is recorded. Public ones call these through `recorded`.
    OpResult setTurtleUnrecorded(const std::string& turtle);
    OpResult addNodeUnrecorded(const std::string& id, const std::string& classIri);
    OpResult removeNodeUnrecorded(const std::string& id);
    OpResult connectUnrecorded(const std::string& fromNode, const std::string& fromPort,
                               const std::string& toNode, const std::string& toPort, std::optional<double> depth);
    OpResult disconnectUnrecorded(const std::string& fromNode, const std::string& fromPort,
                                  const std::string& toNode, const std::string& toPort);
    OpResult applyEditsUnrecorded(const std::vector<GraphEdit>& edits);

    /// Runs an edit and, if it worked, records the circuit it made as a version. Only the outermost
    /// edit records: one that is built from others (a connect is a set_turtle underneath) is one version.
    template <typename Run>
    OpResult recorded(const std::string& label, Run&& run);

    /// Makes the history's current version match the circuit's text, if that text is a circuit.
    /// Returns whether the history now ends at the circuit's text.
    bool syncHistory();

    OpContext ctx;
    int editDepth = 0;
};

}  // namespace valis

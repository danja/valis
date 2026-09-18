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

    double defaultValue = 0.0, minimum = 0.0, maximum = 1.0;
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
};

/// Anything the ops need to reach. Injected rather than owned, so the same ops
/// serve the plugin, the CLI and the tests.
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

    /// Reads and replaces the sound file a val:SampleLoad node is playing. The
    /// document declares one with val:file; this is the session's choice on top
    /// of it, exactly as a turned knob sits on top of a declared value.
    std::function<std::string(const std::string& nodeId)> readSample;
    std::function<bool(const std::string& nodeId, const std::string& path,
                       std::string& error)> writeSample;
};

/// The operations. Each is deliberately small and total: it either does the
/// thing or explains why it could not.
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

    // -- graph editing -----------------------------------------------------
    OpResult addNode(const std::string& id, const std::string& classIri);
    OpResult removeNode(const std::string& id);
    OpResult connect(const std::string& fromNode, const std::string& fromPort,
                     const std::string& toNode,   const std::string& toPort,
                     std::optional<double> depth = std::nullopt);
    OpResult disconnect(const std::string& fromNode, const std::string& fromPort,
                        const std::string& toNode,   const std::string& toPort);

    // -- samples -----------------------------------------------------------
    /// The sound file a node is playing, or a failure if it is not a node that
    /// plays one.
    OpResult getSample(const std::string& nodeId) const;
    OpResult setSample(const std::string& nodeId, const std::string& path);

    // -- parameters --------------------------------------------------------
    std::vector<ParamInfo> listParams() const;
    OpResult getParam(int slot) const;
    OpResult setParam(int slot, double value);

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

    /// Renders the circuit offline into a wav file and reports what came out.
    /// A fresh engine, so it neither touches nor is touched by whatever the
    /// plugin is playing. `note` below zero renders without a note event.
    OpResult render(const std::string& path, double seconds, double sampleRate,
                    int note, double velocity) const;

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
    OpContext ctx;
};

}  // namespace valis

// include/valis/CircuitCompiler.h
//
// Validates a CircuitModel and lowers it to an execution order. Everything that
// can fail fails here, on the message thread, with a diagnostic naming the
// element or arc at fault - never in the audio callback.

#pragma once

#include "valis/CircuitModel.h"

#include <string>
#include <utility>
#include <vector>

namespace valis {

class Ontology;

/// A validated circuit in execution order. The engine consumes this; buffer
/// assignment is added in M4.
struct CompiledCircuit
{
    /// Several arcs arriving at one input are summed into a scratch buffer
    /// before the owning node runs. Only val:Mixer may have one.
    struct SumJob
    {
        int destination = -1;
        std::vector<int> sources;
    };

    struct Node
    {
        std::string id;
        std::string implementation;   ///< ElementRegistry key
        const ElementType* type = nullptr;

        /// Control-input values in the type's control-port order.
        std::vector<double> controlValues;

        /// Instance options, applied through DspElement::setOption.
        std::vector<std::pair<std::string, std::string>> options;

        /// Buffer index per audio port, in the type's declaration order.
        /// An unconnected input reads the shared silence buffer.
        std::vector<int> audioInBuffers;
        std::vector<int> audioOutBuffers;

        /// Slot in the engine's control store, per control output port.
        std::vector<int> controlOutSlots;

        /// Run these before the node processes.
        std::vector<SumJob> sumJobs;
    };

    /// Topologically sorted. A feedback path is cut at its val:UnitDelay, which
    /// reads the previous block's value, so the order is always well defined.
    std::vector<Node> nodes;

    /// Index into `nodes` of the single val:Output, and of the val:Input
    /// elements the host's audio is written into.
    int outputNode = -1;
    std::vector<int> inputNodes;

    /// Sized by the compiler; the engine allocates exactly this much.
    int numBuffers      = 0;
    int numControlSlots = 0;
    int silenceBuffer   = -1;

    /// Audio arcs, as indices into `nodes` plus port symbols. Retained for
    /// the graph view and diagnostics; execution uses the buffer indices.
    struct Link { int from, to; std::string fromPort, toPort; double depth; };
    std::vector<Link> audioLinks;

    /// A control arc, resolved to a source slot and a destination control
    /// input index on the destination node.
    struct ControlLink
    {
        int sourceSlot   = -1;
        int destNode     = -1;
        int destControl  = -1;
        double depth     = 1.0;
        std::string fromPort, toPort;
        int from = -1, to = -1;
    };
    std::vector<ControlLink> controlLinks;

    bool isValid() const { return outputNode >= 0 && ! nodes.empty(); }
};

class CircuitCompiler
{
public:
    /// Returns true only when the circuit is safe to run. `diagnostics` carries
    /// every problem found, not just the first.
    bool compile(const CircuitModel& model,
                 const Ontology& ontology,
                 CompiledCircuit& out,
                 std::vector<Diagnostic>& diagnostics);
};

}  // namespace valis

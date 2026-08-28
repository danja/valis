// include/valis/CircuitCompiler.h
//
// Validates a CircuitModel and lowers it to an execution order. Everything that
// can fail fails here, on the message thread, with a diagnostic naming the
// element or arc at fault - never in the audio callback.

#pragma once

#include "valis/CircuitModel.h"

#include <string>
#include <vector>

namespace valis {

class Ontology;

/// A validated circuit in execution order. The engine consumes this; buffer
/// assignment is added in M4.
struct CompiledCircuit
{
    struct Node
    {
        std::string id;
        std::string implementation;   ///< ElementRegistry key
        const ElementType* type = nullptr;

        /// Control-input values in the type's control-port order.
        std::vector<double> controlValues;
    };

    /// Topologically sorted. A feedback path is cut at its val:UnitDelay, which
    /// reads the previous block's value, so the order is always well defined.
    std::vector<Node> nodes;

    /// Index into `nodes` of the single val:Output.
    int outputNode = -1;

    /// Audio arcs, as indices into `nodes` plus port symbols.
    struct Link { int from, to; std::string fromPort, toPort; double depth; };
    std::vector<Link> audioLinks;
    std::vector<Link> controlLinks;

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

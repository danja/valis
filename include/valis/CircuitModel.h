// include/valis/CircuitModel.h
//
// The in-memory picture of a circuit, resolved against the ontology. Built on
// the message thread from Turtle; the engine never sees this type.

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace valis {

class Ontology;
struct ElementType;
namespace rdf { class TurtleStore; }

/// A diagnostic that points at the thing that is wrong, so the editor can say
/// which node the user needs to look at.
struct Diagnostic
{
    std::string message;
    std::string subject;   ///< IRI of the offending element or arc, may be empty

    /// Source position, when the problem came from the parser rather than from
    /// validation. Zero means "no position", not line zero.
    unsigned line = 0;
    unsigned col  = 0;

    std::string toString() const;
};

struct ElementInstance
{
    std::string id;                       ///< the instance IRI
    std::string typeIri;
    const ElementType* type = nullptr;
    std::string label;

    /// Control-input values set in the Turtle, keyed by port symbol. Ports the
    /// circuit does not mention keep the ontology's lv2:default.
    std::unordered_map<std::string, double> properties;

    /// val: properties that are not control ports - val:antialiasing and the
    /// like. Passed to the element through DspElement::setOption.
    std::unordered_map<std::string, std::string> options;

    double valueOf(const std::string& portSymbol) const;
};

struct Arc
{
    std::string id;
    std::string fromNode, fromPort;
    std::string toNode,   toPort;
    double depth   = 1.0;   ///< val:depth, meaningful on control arcs only
    bool   control = false; ///< resolved from the port rates, not declared
};

struct ParamBinding
{
    int slot = -1;
    std::string targetNode;
    std::string propertySymbol;
    std::string name, symbol, unitSymbol;
    /// When set, overrides the port's lv2:minimum / lv2:maximum for this
    /// particular binding — useful when the same element port (e.g. Scale.min)
    /// is used in circuits where the physical range differs.
    std::optional<double> minimum, maximum;
};

class CircuitModel
{
public:
    /// Reads the single val:Circuit in `store`, resolving every element against
    /// `ontology`. Returns false and fills `diagnostics` if the circuit cannot
    /// be understood at all; recoverable problems are reported by validate().
    bool build(const rdf::TurtleStore& store,
               const Ontology& ontology,
               std::vector<Diagnostic>& diagnostics);

    const std::string& id() const { return circuitId; }
    const std::vector<ElementInstance>& elements() const { return elementList; }
    const std::vector<Arc>& arcs() const { return arcList; }
    const std::vector<ParamBinding>& params() const { return paramList; }

    const ElementInstance* findElement(const std::string& iri) const;

private:
    std::string circuitId;
    std::vector<ElementInstance> elementList;
    std::vector<Arc> arcList;
    std::vector<ParamBinding> paramList;
};

}  // namespace valis

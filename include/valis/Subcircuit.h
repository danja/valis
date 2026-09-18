// include/valis/Subcircuit.h
//
// A val:Subcircuit is a circuit fragment with declared ports that can be
// instantiated as if it were an element. Definitions live in the same document
// as the circuit that uses them, so this table is per document rather than part
// of the ontology.
//
// Nothing below reaches the engine. CircuitModel expands every instance into
// plain elements and arcs before the compiler runs, so the real-time layer sees
// exactly what it saw before subcircuits existed.
//
// Message thread only.

#pragma once

#include "valis/CircuitModel.h"
#include "valis/Ontology.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace valis {

namespace rdf { class TurtleStore; }

/// One declared port of a subcircuit, and the inner port it stands for.
struct SubcircuitPort
{
    PortDesc desc;          ///< what the outside sees, read exactly as an element's port is
    /// Each inner element this port stands for, and the port on it. An input
    /// port naming several is a fan-out: a voice card has one tuning trimmer
    /// and several sounds on it, and one control that reached only the first of
    /// them would be a fault rather than a control. An output port naming
    /// several has no meaning and is reported.
    std::vector<std::pair<std::string, std::string>> targets;

    /// Whether the port declared lv2:default. A declared default is the face
    /// the subcircuit presents and replaces whatever the inner element set; an
    /// undeclared one leaves the inner value alone rather than zeroing it.
    bool hasDefault = false;
};

/// One option a subcircuit exposes, and the inner options it stands for.
///
/// Options are not ports: the ontology declares an element's ports but says
/// nothing about which option keys it accepts, and DspElement::setOption
/// answers true for a key it does not know. So the model cannot tell which
/// inner element "recognises" a key, and a rule that forwarded an option to
/// whichever element wanted it would be guessing.
///
/// The definition names its targets instead, exactly as it does for a port.
/// Two inner elements taking the same key is then not an ambiguity to resolve
/// but something the author asked for: declare both, and both are set.
struct SubcircuitOption
{
    std::string symbol;   ///< the name an instance uses

    /// Each inner element and the option key to set on it. More than one is a
    /// deliberate fan-out, not a collision.
    std::vector<std::pair<std::string, std::string>> targets;
};

/// A subcircuit definition: the ports it exposes and the fragment behind them.
struct SubcircuitDef
{
    std::string id;         ///< the definition's IRI, which is also its rdf:type when instantiated
    std::string label;

    /// The face it presents. `type.implementation` is empty: a subcircuit is
    /// never constructed by ElementRegistry, it is expanded away first.
    ElementType type;

    std::vector<SubcircuitPort> ports;
    std::vector<SubcircuitOption> options;
    std::vector<std::string> elementIris;
    std::vector<std::string> arcIris;

    const SubcircuitPort* findPort(std::string_view symbol) const;
    const SubcircuitOption* findOption(std::string_view symbol) const;
};

/// Every val:Subcircuit in one document.
class SubcircuitLibrary
{
public:
    /// Reads every val:Subcircuit in `store`. A definition that cannot be
    /// understood is reported and skipped; the rest still load, so one bad
    /// fragment does not hide the others.
    bool load(const rdf::TurtleStore& store,
              const Ontology& ontology,
              std::vector<Diagnostic>& diagnostics);

    const SubcircuitDef* find(const std::string& iri) const;

    bool empty() const { return defs.empty(); }
    std::size_t size() const { return defs.size(); }

    /// Definitions in IRI order, so output is deterministic.
    std::vector<const SubcircuitDef*> all() const;

private:
    std::unordered_map<std::string, SubcircuitDef> defs;
};

}  // namespace valis

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
    std::string innerNode;  ///< val:node - the inner element IRI it forwards to
    std::string innerPort;  ///< val:port - that element's port symbol

    /// Whether the port declared lv2:default. A declared default is the face
    /// the subcircuit presents and replaces whatever the inner element set; an
    /// undeclared one leaves the inner value alone rather than zeroing it.
    bool hasDefault = false;
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
    std::vector<std::string> elementIris;
    std::vector<std::string> arcIris;

    const SubcircuitPort* findPort(std::string_view symbol) const;
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

// include/valis/Ontology.h
//
// Reads vocabs/valis.ttl into the element type table the compiler and the DSP
// registry both work from. The ontology is loaded, not merely documented: a
// test asserts that the classes declared here and the factories registered in
// ElementRegistry match in both directions.
//
// Message thread only.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace valis {

namespace rdf { class TurtleStore; }

/// One port of an element type, as declared by lv2:port in the ontology.
struct PortDesc
{
    std::string symbol;      ///< lv2:symbol - the name an arc endpoint uses
    std::string name;        ///< lv2:name, for the knobs view
    bool input   = true;     ///< lv2:InputPort vs lv2:OutputPort
    bool control = false;    ///< lv2:ControlPort vs lv2:AudioPort

    double defaultValue = 0.0;
    double minimum      = 0.0;
    double maximum      = 1.0;
    std::string unitSymbol;  ///< units:symbol, e.g. "Hz", "dB"

    bool logarithmic = false;  ///< lv2:portProperty lv2:logarithmic
    bool enumeration = false;  ///< lv2:portProperty lv2:enumeration

    /// Named integer values, sorted by value. Present when enumeration is true.
    std::vector<std::pair<double, std::string>> scalePoints;

    bool isAudio() const { return ! control; }
};

/// An instantiable element class.
struct ElementType
{
    std::string classIri;
    std::string implementation;   ///< val:implementation - an ElementRegistry key
    std::string label;
    std::string antialiasing;     ///< val:antialiasing IRI, empty if not declared
    bool linear = true;
    std::vector<PortDesc> ports;

    const PortDesc* findPort(std::string_view symbol) const;

    /// Ports matching a direction and rate, in declaration order.
    std::vector<const PortDesc*> portsMatching(bool input, bool control) const;

    /// Control inputs are settable properties: val:cutoff and the port symbol
    /// "cutoff" are the same thing.
    const PortDesc* findProperty(std::string_view localName) const;
};

class Ontology
{
public:
    /// Appends to `errors` and returns false if the file will not parse or
    /// declares a class the loader cannot make sense of.
    bool loadFile(const std::string& path, std::vector<std::string>& errors);

    /// Loads an LV2 units vocabulary so named units such as units:hz resolve to
    /// their real symbols ("Hz") rather than to the local name of their IRI.
    ///
    /// Call this BEFORE loadFile: symbols are resolved as ports are read, so a
    /// units file loaded afterwards has no effect on what is already loaded.
    /// Optional - without it units still work, they just read less well.
    bool loadUnits(const std::string& path, std::vector<std::string>& errors);
    bool loadTurtle(std::string_view turtle, std::vector<std::string>& errors);

    /// Resolves owl:equivalentClass aliases, so val:NonLinear finds val:Transfer.
    const ElementType* find(std::string_view classIri) const;

    /// Every implementable class, ordered by IRI so output is deterministic.
    std::vector<const ElementType*> types() const;

    /// The val:implementation keys, sorted. The registry must match this set.
    std::vector<std::string> implementationKeys() const;

    std::size_t size() const { return typesByIri.size(); }

private:
    bool loadFromStore(const rdf::TurtleStore&, std::vector<std::string>& errors);

    std::unordered_map<std::string, ElementType> typesByIri;
    std::unordered_map<std::string, std::string> aliases;    ///< alias IRI -> canonical
    std::unordered_map<std::string, std::string> unitSymbols; ///< unit IRI -> symbol
};

}  // namespace valis

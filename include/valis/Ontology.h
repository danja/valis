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

namespace rdf { class TurtleStore; class Node; }

/// One port of an element type, as declared by lv2:port in the ontology.
struct PortDesc
{
    std::string symbol;      ///< lv2:symbol - the name an arc endpoint uses
    std::string name;        ///< lv2:name, for the knobs view
    bool input   = true;     ///< lv2:InputPort vs lv2:OutputPort
    bool control = false;    ///< lv2:ControlPort vs lv2:AudioPort

    /// atom:AtomPort - a port carrying timed events rather than a signal. An
    /// event port has no buffer and no control slot, so countPorts(),
    /// portsMatching() and the compiler's buffer assignment all pass it by; it
    /// is declared so the ontology describes what the element really does, and
    /// so a view can draw it.
    bool event = false;

    double defaultValue = 0.0;
    double minimum      = 0.0;
    double maximum      = 1.0;
    std::string unitSymbol;  ///< units:symbol, e.g. "Hz", "dB"

    bool logarithmic = false;  ///< lv2:portProperty lv2:logarithmic
    bool enumeration = false;  ///< lv2:portProperty lv2:enumeration
    bool toggled     = false;  ///< lv2:portProperty lv2:toggled - a binary choice

    /// Whether the port offers a fixed set of choices rather than a range, and
    /// how many. A binary port counts as two choices whether it was declared
    /// lv2:toggled or as an enumeration with two scale points, so the view that
    /// draws it does not have to know which spelling was used.
    bool isChoice() const { return toggled || (enumeration && scalePoints.size() > 1); }
    bool isBinary() const { return toggled || (enumeration && scalePoints.size() == 2); }

    /// Named integer values, sorted by value. Present when enumeration is true.
    std::vector<std::pair<double, std::string>> scalePoints;

    bool isAudio() const { return ! control && ! event; }
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
    const PortDesc* findPort(std::string_view symbol, bool input, bool control) const;

    /// Ports matching a direction and rate, in declaration order.
    std::vector<const PortDesc*> portsMatching(bool input, bool control) const;

    int countPorts(bool input, bool control) const;

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

    /// Reads one lv2:port description, resolving named units against whatever
    /// loadUnits() supplied. A val:Subcircuit declares its ports exactly as an
    /// element class does, so the two share this reader rather than repeating
    /// it. Returns nothing when the node is not a usable port.
    std::optional<PortDesc> readPort(const rdf::TurtleStore& store,
                                     const rdf::Node& port) const;

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

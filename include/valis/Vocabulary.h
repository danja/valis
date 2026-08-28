// include/valis/Vocabulary.h
//
// The single source of IRI truth. Frozen string constants, no namespace-builder
// machinery. Only terms the C++ code names directly live here; element-specific
// properties (val:cutoff, val:wave, ...) are read dynamically from the ontology
// and never need a constant.

#pragma once

#include <string>
#include <string_view>

namespace valis::vocab {

// One namespace, trailing slash, decided once.
inline constexpr std::string_view VAL   = "http://purl.org/stuff/valis/";
inline constexpr std::string_view RDF   = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
inline constexpr std::string_view RDFS  = "http://www.w3.org/2000/01/rdf-schema#";
inline constexpr std::string_view OWL   = "http://www.w3.org/2002/07/owl#";
inline constexpr std::string_view XSD   = "http://www.w3.org/2001/XMLSchema#";
inline constexpr std::string_view LV2   = "http://lv2plug.in/ns/lv2core#";
inline constexpr std::string_view UNITS = "http://lv2plug.in/ns/extensions/units#";

// Concatenating string_views needs a helper; these are consteval-friendly enough
// as std::string at namespace scope, and are only touched off the audio thread.
#define VALIS_TERM(ns, local) inline const std::string local = std::string(ns) + #local

namespace rdf {
  inline const std::string type  = std::string(RDF) + "type";
  inline const std::string first = std::string(RDF) + "first";
  inline const std::string rest  = std::string(RDF) + "rest";
  inline const std::string nil   = std::string(RDF) + "nil";
}

namespace rdfs {
  inline const std::string Class      = std::string(RDFS) + "Class";
  inline const std::string subClassOf = std::string(RDFS) + "subClassOf";
  inline const std::string label      = std::string(RDFS) + "label";
  inline const std::string comment    = std::string(RDFS) + "comment";
}

namespace owl {
  inline const std::string equivalentClass = std::string(OWL) + "equivalentClass";
}

// LV2 terms carry the port and control-range description, as the brief asks.
namespace lv2 {
  inline const std::string port        = std::string(LV2) + "port";
  inline const std::string symbol      = std::string(LV2) + "symbol";
  inline const std::string name        = std::string(LV2) + "name";
  inline const std::string index       = std::string(LV2) + "index";
  inline const std::string defaultV    = std::string(LV2) + "default";
  inline const std::string minimum     = std::string(LV2) + "minimum";
  inline const std::string maximum     = std::string(LV2) + "maximum";
  inline const std::string InputPort   = std::string(LV2) + "InputPort";
  inline const std::string OutputPort  = std::string(LV2) + "OutputPort";
  inline const std::string AudioPort   = std::string(LV2) + "AudioPort";
  inline const std::string ControlPort = std::string(LV2) + "ControlPort";
}

namespace units {
  inline const std::string unit   = std::string(UNITS) + "unit";
  inline const std::string render = std::string(UNITS) + "render";
  inline const std::string symbol = std::string(UNITS) + "symbol";
}

namespace val {
  // Classes
  inline const std::string Circuit    = std::string(VAL) + "Circuit";
  inline const std::string Element    = std::string(VAL) + "Element";
  inline const std::string Filter     = std::string(VAL) + "Filter";
  inline const std::string Transfer   = std::string(VAL) + "Transfer";
  inline const std::string NonLinear  = std::string(VAL) + "NonLinear";  // alias of Transfer
  inline const std::string Source     = std::string(VAL) + "Source";
  inline const std::string Arc        = std::string(VAL) + "Arc";
  inline const std::string Param      = std::string(VAL) + "Param";

  // Structure
  inline const std::string element        = std::string(VAL) + "element";
  inline const std::string arc            = std::string(VAL) + "arc";
  inline const std::string from           = std::string(VAL) + "from";
  inline const std::string to             = std::string(VAL) + "to";
  inline const std::string node           = std::string(VAL) + "node";
  inline const std::string port           = std::string(VAL) + "port";
  inline const std::string implementation = std::string(VAL) + "implementation";
  inline const std::string linear         = std::string(VAL) + "linear";

  // Parameter binding
  inline const std::string slot     = std::string(VAL) + "slot";
  inline const std::string target   = std::string(VAL) + "target";
  inline const std::string property = std::string(VAL) + "property";

  // Editor metadata. Kept in a separate graph from execution metadata: moving a
  // node must never invalidate the compiled circuit.
  inline const std::string x = std::string(VAL) + "x";
  inline const std::string y = std::string(VAL) + "y";
}

#undef VALIS_TERM

/// Builds a `val:` term IRI from its local name. The dynamic property lookup
/// path uses this constantly, since `val:cutoff` and the name "cutoff" are the
/// same thing. Inverse of shortName() for terms in our own namespace.
std::string valTerm(std::string_view localName);

/// The local part of an IRI - everything after the last '/' or '#'.
/// `val:cutoff` and the C++ property name "cutoff" are thereby the same thing.
std::string shortName(std::string_view iri);

/// Expands a `prefix:local` name against the namespaces above. Returns the input
/// unchanged if the prefix is unknown, so absolute IRIs pass through untouched.
std::string expand(std::string_view curie);

}  // namespace valis::vocab

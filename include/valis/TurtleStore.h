// include/valis/TurtleStore.h
//
// RAII wrappers over sord's in-memory quad store and serd's Turtle reader.
// Parse errors carry serd's line and column so the editor can mark the gutter.
// Message thread only: nothing here is safe to call from the audio callback.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SordWorldImpl;
struct SordModelImpl;
struct SordNodeImpl;

namespace valis::rdf {

struct ParseError
{
    std::string message;
    unsigned    line = 0;   ///< 1-based; 0 when serd reports no position
    unsigned    col  = 0;   ///< 1-based

    std::string toString() const;
};

/// A node borrowed from the store. Valid while the owning TurtleStore lives.
class Node
{
public:
    Node() = default;
    explicit Node(const SordNodeImpl* n) : node(n) {}

    bool isValid() const { return node != nullptr; }
    explicit operator bool() const { return isValid(); }

    bool isUri() const;
    bool isBlank() const;
    bool isLiteral() const;

    /// The lexical form: the IRI, the blank node id, or the literal's text.
    std::string_view string() const;

    /// The datatype IRI of a literal, or an empty view if there is none.
    std::string_view datatype() const;

    std::optional<double>  asDouble() const;
    std::optional<int64_t> asInt() const;
    std::optional<bool>    asBool() const;

    bool operator==(const Node& other) const;
    bool operator!=(const Node& other) const { return ! (*this == other); }

    const SordNodeImpl* raw() const { return node; }

private:
    const SordNodeImpl* node = nullptr;
};

class TurtleStore
{
public:
    TurtleStore();
    ~TurtleStore();

    TurtleStore(const TurtleStore&) = delete;
    TurtleStore& operator=(const TurtleStore&) = delete;
    TurtleStore(TurtleStore&&) noexcept;
    TurtleStore& operator=(TurtleStore&&) noexcept;

    /// Parses Turtle into the store, adding to whatever is already there.
    /// Returns false if any error was reported; `errors` is appended to either
    /// way, since serd recovers from some errors and keeps reading.
    bool parse(std::string_view turtle,
               std::string_view baseUri,
               std::vector<ParseError>& errors);

    bool parseFile(const std::string& path, std::vector<ParseError>& errors);

    /// Serialises the whole store back to Turtle, with the prefixes registered
    /// by registerPrefix(). Structural graph edits round-trip through this, so
    /// hand-written comments and layout are not preserved.
    std::string serialise() const;

    void registerPrefix(std::string_view prefix, std::string_view uri);

    // -- node construction ------------------------------------------------
    Node uri(std::string_view iri) const;
    Node blank(std::string_view id) const;
    Node literal(std::string_view text, std::string_view datatypeIri = {}) const;

    // -- queries ----------------------------------------------------------
    /// The single object of (subject, predicate), or an invalid Node. When more
    /// than one exists, which one is returned is unspecified - use objects().
    Node object(const Node& subject, std::string_view predicate) const;

    std::vector<Node> objects(const Node& subject, std::string_view predicate) const;
    std::vector<Node> subjects(std::string_view predicate, const Node& object) const;

    /// Every subject with `rdf:type <typeIri>`, including via rdfs:subClassOf
    /// when `includeSubclasses` is set.
    std::vector<Node> subjectsOfType(std::string_view typeIri) const;

    bool contains(const Node& subject, std::string_view predicate, const Node& object) const;

    /// Visits every statement whose subject is `subject`.
    void forEachProperty(const Node& subject,
                         const std::function<void(const Node& predicate,
                                                  const Node& object)>& fn) const;

    void add(const Node& subject, const Node& predicate, const Node& object);
    void remove(const Node& subject, const Node& predicate, const Node& object);

    std::size_t size() const;

    SordWorldImpl* world() const { return worldPtr; }
    SordModelImpl* model() const { return modelPtr; }

private:
    SordWorldImpl* worldPtr = nullptr;
    SordModelImpl* modelPtr = nullptr;
    std::vector<std::pair<std::string, std::string>> prefixes;

    /// Errors for the parse currently in flight. Some diagnostics - a CURIE
    /// that will not expand, for one - are raised by sord's world rather than
    /// by the reader, so both sinks have to reach the same place.
    std::vector<ParseError>* currentErrors = nullptr;
};

}  // namespace valis::rdf

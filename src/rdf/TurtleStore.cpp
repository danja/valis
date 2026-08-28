// src/rdf/TurtleStore.cpp

#include "valis/TurtleStore.h"

#include <serd/serd.h>
#include <sord/sord.h>

#include <algorithm>
#include <charconv>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>

namespace valis::rdf {

namespace {

const uint8_t* u8(std::string_view s)
{
    return reinterpret_cast<const uint8_t*>(s.data());
}

/// serd hands us a printf-style format plus its va_list. Render it once here so
/// the rest of the codebase never sees varargs.
SerdStatus collectInto(std::vector<ParseError>* errors, const SerdError* error)
{
    if (errors == nullptr)
        return SERD_SUCCESS;

    char buffer[512] = {};
    if (error->fmt != nullptr)
    {
        va_list args;
        va_copy(args, *error->args);
        std::vsnprintf(buffer, sizeof(buffer), error->fmt, args);
        va_end(args);
    }

    std::string message(buffer);
    // serd's messages end in a newline; the editor gutter does not want it.
    while (! message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.pop_back();

    errors->push_back(ParseError{std::move(message), error->line, error->col});
    return SERD_SUCCESS;
}

SerdStatus collectError(void* handle, const SerdError* error)
{
    return collectInto(static_cast<std::vector<ParseError>*>(handle), error);
}

/// The world outlives any one parse, so it reports through the store, which
/// knows which vector is currently collecting.
SerdStatus collectWorldError(void* handle, const SerdError* error)
{
    return collectInto(*static_cast<std::vector<ParseError>**>(handle), error);
}

std::string_view nodeString(const SordNode* n)
{
    if (n == nullptr)
        return {};

    std::size_t bytes = 0;
    const auto* s = sord_node_get_string_counted(n, &bytes);
    return {reinterpret_cast<const char*>(s), bytes};
}

}  // namespace

namespace {
SordWorld* sw(SordWorldImpl* w) { return reinterpret_cast<SordWorld*>(w); }
SordModel* sm(SordModelImpl* m) { return reinterpret_cast<SordModel*>(m); }
}  // namespace

// ---------------------------------------------------------------------------
// ParseError
// ---------------------------------------------------------------------------

std::string ParseError::toString() const
{
    if (line == 0)
        return message;

    return std::to_string(line) + ":" + std::to_string(col) + ": " + message;
}

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------

namespace {
const SordNode* sn(const SordNodeImpl* n) { return reinterpret_cast<const SordNode*>(n); }
}  // namespace

bool Node::isUri() const     { return node != nullptr && sord_node_get_type(sn(node)) == SORD_URI; }
bool Node::isBlank() const   { return node != nullptr && sord_node_get_type(sn(node)) == SORD_BLANK; }
bool Node::isLiteral() const { return node != nullptr && sord_node_get_type(sn(node)) == SORD_LITERAL; }

std::string_view Node::string() const { return nodeString(sn(node)); }

std::string_view Node::datatype() const
{
    if (node == nullptr)
        return {};

    return nodeString(sord_node_get_datatype(sn(node)));
}

std::optional<double> Node::asDouble() const
{
    const auto s = string();
    if (s.empty())
        return std::nullopt;

    // from_chars for double is available in libstdc++ 11+; fall back is not needed.
    double value = 0.0;
    const auto* begin = s.data();
    const auto* end   = s.data() + s.size();
    if (auto [ptr, ec] = std::from_chars(begin, end, value); ec == std::errc{} && ptr == end)
        return value;

    return std::nullopt;
}

std::optional<int64_t> Node::asInt() const
{
    const auto s = string();
    if (s.empty())
        return std::nullopt;

    int64_t value = 0;
    const auto* begin = s.data();
    const auto* end   = s.data() + s.size();
    if (auto [ptr, ec] = std::from_chars(begin, end, value); ec == std::errc{} && ptr == end)
        return value;

    return std::nullopt;
}

std::optional<bool> Node::asBool() const
{
    const auto s = string();
    if (s == "true"  || s == "1") return true;
    if (s == "false" || s == "0") return false;
    return std::nullopt;
}

bool Node::operator==(const Node& other) const
{
    // Terms are compared with sord_node_equals, never pointer identity.
    if (node == nullptr || other.node == nullptr)
        return node == other.node;

    return sord_node_equals(sn(node), sn(other.node));
}

// ---------------------------------------------------------------------------
// TurtleStore
// ---------------------------------------------------------------------------

TurtleStore::TurtleStore()
{
    auto* w = sord_world_new();
    // OPS and POS are both needed: subjects() queries (? p o) and
    // subjectsOfType() queries (? rdf:type T).
    auto* m = sord_new(w, SORD_SPO | SORD_OPS | SORD_POS, false);

    worldPtr = reinterpret_cast<SordWorldImpl*>(w);
    modelPtr = reinterpret_cast<SordModelImpl*>(m);

    sord_world_set_error_sink(w, collectWorldError, &currentErrors);
}

TurtleStore::~TurtleStore()
{
    if (modelPtr != nullptr)
        sord_free(sm(modelPtr));
    if (worldPtr != nullptr)
        sord_world_free(sw(worldPtr));
}

TurtleStore::TurtleStore(TurtleStore&& other) noexcept
    : worldPtr(std::exchange(other.worldPtr, nullptr)),
      modelPtr(std::exchange(other.modelPtr, nullptr)),
      prefixes(std::move(other.prefixes))
{
    if (worldPtr != nullptr)
        sord_world_set_error_sink(sw(worldPtr), collectWorldError, &currentErrors);
}

TurtleStore& TurtleStore::operator=(TurtleStore&& other) noexcept
{
    if (this != &other)
    {
        if (modelPtr != nullptr) sord_free(sm(modelPtr));
        if (worldPtr != nullptr) sord_world_free(sw(worldPtr));

        worldPtr = std::exchange(other.worldPtr, nullptr);
        modelPtr = std::exchange(other.modelPtr, nullptr);
        prefixes = std::move(other.prefixes);

        if (worldPtr != nullptr)
            sord_world_set_error_sink(sw(worldPtr), collectWorldError, &currentErrors);
    }
    return *this;
}

bool TurtleStore::parse(std::string_view turtle,
                        std::string_view baseUri,
                        std::vector<ParseError>& errors)
{
    currentErrors = &errors;
    struct Scope
    {
        std::vector<ParseError>** slot;
        ~Scope() { *slot = nullptr; }
    } scope{&currentErrors};

    const std::string base(baseUri);
    SerdNode baseNode = serd_node_from_string(SERD_URI, u8(base));
    SerdEnv* env = serd_env_new(&baseNode);

    SerdReader* reader = sord_new_reader(sm(modelPtr), env, SERD_TURTLE, nullptr);
    serd_reader_set_error_sink(reader, collectError, &errors);

    // serd_reader_read_string needs a null-terminated buffer.
    const std::string text(turtle);
    const SerdStatus status = serd_reader_read_string(reader, u8(text));

    serd_reader_free(reader);
    serd_env_free(env);

    return status == SERD_SUCCESS && errors.empty();
}

bool TurtleStore::parseFile(const std::string& path, std::vector<ParseError>& errors)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr)
    {
        errors.push_back(ParseError{"cannot open " + path, 0, 0});
        return false;
    }

    std::string text;
    char buffer[4096];
    while (const auto n = std::fread(buffer, 1, sizeof(buffer), f))
        text.append(buffer, n);
    std::fclose(f);

    return parse(text, "file://" + path, errors);
}

void TurtleStore::registerPrefix(std::string_view prefix, std::string_view uri)
{
    const std::string p(prefix);
    auto it = std::find_if(prefixes.begin(), prefixes.end(),
                           [&](const auto& e) { return e.first == p; });

    if (it != prefixes.end())
        it->second = std::string(uri);
    else
        prefixes.emplace_back(p, std::string(uri));
}

std::string TurtleStore::serialise() const
{
    SerdEnv* env = serd_env_new(nullptr);

    SerdChunk chunk = {nullptr, 0};
    SerdWriter* writer = serd_writer_new(
        SERD_TURTLE,
        static_cast<SerdStyle>(SERD_STYLE_ABBREVIATED | SERD_STYLE_CURIED),
        env, nullptr, serd_chunk_sink, &chunk);

    for (const auto& [prefix, uri] : prefixes)
    {
        SerdNode name = serd_node_from_string(SERD_LITERAL, u8(prefix));
        SerdNode ns   = serd_node_from_string(SERD_URI, u8(uri));
        serd_env_set_prefix(env, &name, &ns);
        serd_writer_set_prefix(writer, &name, &ns);
    }

    sord_write(sm(modelPtr), writer, nullptr);
    serd_writer_finish(writer);

    uint8_t* out = serd_chunk_sink_finish(&chunk);
    std::string result = out != nullptr ? reinterpret_cast<const char*>(out) : "";

    serd_free(out);
    serd_writer_free(writer);
    serd_env_free(env);

    return result;
}

// -- node construction ------------------------------------------------------
//
// sord interns nodes in the world and refcounts them. Nodes reachable from a
// statement in the model are kept alive by it; a node created but never added
// would leak. Creation is rare and off the audio thread, so we accept the leak
// of unused nodes rather than complicate Node with ownership.

Node TurtleStore::uri(std::string_view iri) const
{
    const std::string s(iri);
    return Node{reinterpret_cast<const SordNodeImpl*>(sord_new_uri(sw(worldPtr), u8(s)))};
}

Node TurtleStore::blank(std::string_view id) const
{
    const std::string s(id);
    return Node{reinterpret_cast<const SordNodeImpl*>(sord_new_blank(sw(worldPtr), u8(s)))};
}

Node TurtleStore::literal(std::string_view text, std::string_view datatypeIri) const
{
    const std::string s(text);
    SordNode* dt = nullptr;
    std::string dts;

    if (! datatypeIri.empty())
    {
        dts = std::string(datatypeIri);
        dt  = sord_new_uri(sw(worldPtr), u8(dts));
    }

    auto* n = sord_new_literal(sw(worldPtr), dt, u8(s), nullptr);
    return Node{reinterpret_cast<const SordNodeImpl*>(n)};
}

// -- queries ----------------------------------------------------------------

Node TurtleStore::object(const Node& subject, std::string_view predicate) const
{
    if (! subject)
        return {};

    const std::string p(predicate);
    auto* pred = sord_new_uri(sw(worldPtr), u8(p));
    auto* iter = sord_search(sm(modelPtr), sn(subject.raw()), pred, nullptr, nullptr);

    Node result;
    if (iter != nullptr && ! sord_iter_end(iter))
        result = Node{reinterpret_cast<const SordNodeImpl*>(
            sord_iter_get_node(iter, SORD_OBJECT))};

    sord_iter_free(iter);
    sord_node_free(sw(worldPtr), pred);
    return result;
}

std::vector<Node> TurtleStore::objects(const Node& subject, std::string_view predicate) const
{
    std::vector<Node> result;
    if (! subject)
        return result;

    const std::string p(predicate);
    auto* pred = sord_new_uri(sw(worldPtr), u8(p));

    auto* iter = sord_search(sm(modelPtr), sn(subject.raw()), pred, nullptr, nullptr);
    while (iter != nullptr && ! sord_iter_end(iter))
    {
        result.emplace_back(reinterpret_cast<const SordNodeImpl*>(
            sord_iter_get_node(iter, SORD_OBJECT)));
        sord_iter_next(iter);
    }
    sord_iter_free(iter);

    sord_node_free(sw(worldPtr), pred);
    return result;
}

std::vector<Node> TurtleStore::subjects(std::string_view predicate, const Node& object) const
{
    std::vector<Node> result;
    if (! object)
        return result;

    const std::string p(predicate);
    auto* pred = sord_new_uri(sw(worldPtr), u8(p));

    auto* iter = sord_search(sm(modelPtr), nullptr, pred, sn(object.raw()), nullptr);
    while (iter != nullptr && ! sord_iter_end(iter))
    {
        result.emplace_back(reinterpret_cast<const SordNodeImpl*>(
            sord_iter_get_node(iter, SORD_SUBJECT)));
        sord_iter_next(iter);
    }
    sord_iter_free(iter);

    sord_node_free(sw(worldPtr), pred);
    return result;
}

std::vector<Node> TurtleStore::subjectsOfType(std::string_view typeIri) const
{
    const std::string t(typeIri);
    auto* type = sord_new_uri(sw(worldPtr), u8(t));

    auto result = subjects("http://www.w3.org/1999/02/22-rdf-syntax-ns#type",
                           Node{reinterpret_cast<const SordNodeImpl*>(type)});

    sord_node_free(sw(worldPtr), type);
    return result;
}

bool TurtleStore::contains(const Node& subject,
                           std::string_view predicate,
                           const Node& object) const
{
    if (! subject || ! object)
        return false;

    const std::string p(predicate);
    auto* pred = sord_new_uri(sw(worldPtr), u8(p));
    const bool found = sord_ask(sm(modelPtr), sn(subject.raw()), pred,
                                sn(object.raw()), nullptr);
    sord_node_free(sw(worldPtr), pred);
    return found;
}

void TurtleStore::forEachProperty(
    const Node& subject,
    const std::function<void(const Node&, const Node&)>& fn) const
{
    if (! subject)
        return;

    auto* iter = sord_search(sm(modelPtr), sn(subject.raw()), nullptr, nullptr, nullptr);
    while (iter != nullptr && ! sord_iter_end(iter))
    {
        fn(Node{reinterpret_cast<const SordNodeImpl*>(sord_iter_get_node(iter, SORD_PREDICATE))},
           Node{reinterpret_cast<const SordNodeImpl*>(sord_iter_get_node(iter, SORD_OBJECT))});
        sord_iter_next(iter);
    }
    sord_iter_free(iter);
}

void TurtleStore::add(const Node& subject, const Node& predicate, const Node& object)
{
    if (! subject || ! predicate || ! object)
        return;

    const SordQuad quad = {sn(subject.raw()), sn(predicate.raw()), sn(object.raw()), nullptr};
    sord_add(sm(modelPtr), quad);
}

void TurtleStore::remove(const Node& subject, const Node& predicate, const Node& object)
{
    if (! subject || ! predicate || ! object)
        return;

    const SordQuad quad = {sn(subject.raw()), sn(predicate.raw()), sn(object.raw()), nullptr};
    sord_remove(sm(modelPtr), quad);
}

std::size_t TurtleStore::size() const
{
    return static_cast<std::size_t>(sord_num_quads(sm(modelPtr)));
}

}  // namespace valis::rdf

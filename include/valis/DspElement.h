// include/valis/DspElement.h
//
// The interface every circuit element implements. Real-time contract: process()
// must not allocate, do I/O, take a lock, or throw. Everything expensive
// happens in prepare(), on the message thread.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace valis {

struct ElementType;

/// Buffers for one block. Audio arrays are indexed by the element type's audio
/// port declaration order; control arrays by its control port order. An input
/// with no arc attached points at a block of silence, never at null.
struct ProcessArgs
{
    const float* const* audioIn  = nullptr;
    float* const*       audioOut = nullptr;
    int numAudioIn  = 0;
    int numAudioOut = 0;
    int numSamples  = 0;

    /// Whether any note is held, and the velocity of the most recent one.
    /// Envelopes and gated elements read these; everything else ignores them.
    bool  gate     = false;
    float velocity = 0.0f;

    /// One value per control input, already resolved for this block: the
    /// element's own property, overridden by any control arc reaching it.
    const float* controlIn  = nullptr;
    float*       controlOut = nullptr;
    int numControlIn  = 0;
    int numControlOut = 0;
};

/// Defaults degrade gracefully rather than abort: an element that does not care
/// about a call should not have to implement it.
class DspElement
{
public:
    virtual ~DspElement() = default;

    /// Called on the message thread before the element joins the running graph.
    /// Allocate here or not at all.
    virtual void prepare(const ElementType& type, double sampleRate, int maxBlockSize)
    {
        (void) type; (void) sampleRate; (void) maxBlockSize;
    }

    /// An option set on this instance in the Turtle, applied after prepare()
    /// and before the element runs. Keys are val: local names; an element
    /// ignores what it does not recognise.
    virtual void setOption(std::string_view key, std::string_view value)
    {
        (void) key; (void) value;
    }

    /// Clear state without reallocating. Called when the transport relocates.
    virtual void reset() {}

    virtual void process(const ProcessArgs& args) noexcept = 0;

    /// Extra latency this element introduces, in samples.
    virtual int latencyInSamples() const { return 0; }
};

/// Maps a val:implementation key to a factory. Not a singleton: the registry is
/// constructed and injected, so a test can build a partial one.
class ElementRegistry
{
public:
    using Factory = std::unique_ptr<DspElement> (*)();

    void add(std::string key, Factory factory);

    std::unique_ptr<DspElement> create(std::string_view key) const;
    bool contains(std::string_view key) const;

    /// Sorted, so it can be compared against the ontology's key set directly.
    std::vector<std::string> keys() const;

    std::size_t size() const { return factories.size(); }

private:
    std::vector<std::pair<std::string, Factory>> factories;
};

/// Every element the ontology declares. The set of keys here and the set of
/// val:implementation values in vocabs/valis.ttl must match exactly - a test
/// asserts it in both directions, so drift fails the build.
ElementRegistry makeDefaultRegistry();

}  // namespace valis

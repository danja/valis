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

/// The host's timeline for the current block. `ppqPosition` counts quarter
/// notes from the start of the timeline and advances across the block, so an
/// element can derive a musical phase from it without counting samples itself.
/// When the host is stopped, or provides no timeline at all, `playing` is false
/// and `tempoBpm` is the last value the host reported.
struct TransportInfo
{
    bool   playing            = false;
    double tempoBpm           = 120.0;
    double ppqPosition        = 0.0;
    double ppqPositionOfBar   = 0.0;
    int    timeSigNumerator   = 4;
    int    timeSigDenominator = 4;
};

/// A note event an element produces, on its way to the host. Located by its
/// position in the host's block, like every other event: an element writes the
/// offset within the slice it is given, and emit() rebases it.
struct ElementEvent
{
    int   sampleOffset = 0;
    bool  noteOn       = true;
    int   note         = 60;      ///< MIDI note number, 0-127
    float velocity     = 1.0f;    ///< 0-1
    int   channel      = 1;       ///< MIDI channel, 1-16
};

/// Buffers for one block. Audio arrays are indexed by the element type's audio
/// port declaration order; control arrays by its control port order. An input
/// with no arc attached points at a block of silence, never at null.
struct ProcessArgs
{
    const float* const* audioIn  = nullptr;
    float* const*       audioOut = nullptr;

    /// The block of silence every unconnected input points at. An element that
    /// must tell "nothing is wired here" from "what is wired here is quiet"
    /// compares its input pointer against this. Null in a fixture that supplies
    /// no silence buffer, which reads as every input being connected.
    const float* silence = nullptr;
    int numAudioIn  = 0;
    int numAudioOut = 0;
    int numSamples  = 0;

    /// Whether any note is held, the velocity and MIDI note number of the most
    /// recent note-on. Envelopes and pitch/velocity sources read these.
    bool         gate           = false;
    float        velocity       = 0.0f;
    int          noteNumber     = 69;   ///< default A4 so MidiPitch outputs 440 Hz before any note
    const float* noteVelocities = nullptr;  ///< Array of 128 per-MIDI-note velocities (0..127)

    /// The host timeline, already advanced to the start of this block.
    TransportInfo transport;

    /// One value per control input, already resolved for this block: the
    /// element's own property, overridden by any control arc reaching it.
    const float* controlIn  = nullptr;
    float*       controlOut = nullptr;
    int numControlIn  = 0;
    int numControlOut = 0;

    /// Where an element declaring an atom:AtomPort output writes the events it
    /// produces. One sink is shared by the whole circuit for the block, so an
    /// element does not need a port index to write to it. Null in a fixture
    /// that supplies none, which reads as "nowhere to send them".
    ElementEvent* eventsOut   = nullptr;
    int*          numEventsOut = nullptr;
    int           eventCapacity = 0;

    /// Where this slice starts within the host's block, so an event an element
    /// locates within its own slice comes out located within the block.
    int sliceOffset = 0;

    /// Emits one event. Bounded and allocation-free: past the sink's capacity
    /// the event is dropped and false returned, which is the right trade on
    /// this thread. `sampleOffset` is relative to this slice.
    bool emit(const ElementEvent& event) const noexcept
    {
        if (eventsOut == nullptr || numEventsOut == nullptr || *numEventsOut >= eventCapacity)
            return false;

        ElementEvent located = event;
        located.sampleOffset += sliceOffset;
        eventsOut[(*numEventsOut)++] = located;
        return true;
    }
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
    ///
    /// Returns false only when the key was recognised and could not be applied
    /// - a sample file that does not exist, say - and writes the reason into
    /// `error`. The circuit then fails to load with that message rather than
    /// running silently wrong. An unknown key is not a failure.
    virtual bool setOption(std::string_view key, std::string_view value, std::string& error)
    {
        (void) key; (void) value; (void) error;
        return true;
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

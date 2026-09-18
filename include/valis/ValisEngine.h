// include/valis/ValisEngine.h
//
// Owns the running circuit. Two threads meet here, and the contract between
// them is the whole point of the class:
//
//   message thread  load(), collectGarbage(), prepare()
//   audio thread    process() only
//
// process() allocates nothing, takes no lock, does no I/O and never parses RDF.
// A new circuit is built entirely on the message thread and installed with one
// atomic exchange; the retired one is freed later, by the message thread, once
// the audio thread has demonstrably moved past it.

#pragma once

#include "valis/CircuitCompiler.h"
#include "valis/DspElement.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace valis {

class ValisEngine
{
public:
    ValisEngine();
    ~ValisEngine();

    ValisEngine(const ValisEngine&) = delete;
    ValisEngine& operator=(const ValisEngine&) = delete;

    /// Message thread. Must be called before load().
    void prepare(double sampleRate, int maxBlockSize);

    /// Message thread. Instantiates every element, allocates every buffer, then
    /// installs the result atomically. The previously active graph keeps running
    /// until the exchange completes.
    bool load(const CompiledCircuit& circuit,
              const ElementRegistry& registry,
              std::string& error);

    /// Audio thread, before process(). A note the circuit's envelopes and
    /// oscillators can respond to, taking effect at the start of the next
    /// block. This is what the editor's virtual keyboard uses, where there is
    /// no meaningful position within a block.
    void noteOn(int noteNumber, float velocity) noexcept;
    void noteOff(int noteNumber) noexcept;
    void allNotesOff() noexcept;

    /// Audio thread, before process(). A note event located by its position
    /// within the block about to run, which is what the host reports for each
    /// MIDI message. process() cuts its slices at these positions, so the note
    /// starts on the sample it was sent on rather than at the block boundary.
    ///
    /// Bounded: beyond kMaxEventsPerBlock the extra events are dropped rather
    /// than allocating, which is the right trade on this thread.
    void queueNoteOn(int noteNumber, float velocity, int sampleOffset) noexcept;
    void queueNoteOff(int noteNumber, int sampleOffset) noexcept;
    void queueAllNotesOff(int sampleOffset) noexcept;

    /// Audio thread, before process(). The host's timeline for the block about
    /// to run. Elements see it advance across the block: process() carries the
    /// position forward one control slice at a time from the tempo, so a
    /// musical phase does not step at buffer boundaries.
    void setTransport(const TransportInfo& info) noexcept { transport = info; }
    const TransportInfo& currentTransport() const noexcept { return transport; }

    /// Audio thread. Any pointer may be null: a null input is silence, and a
    /// null output is simply not written. The two-input form carries the host's
    /// channels separately, so a circuit taking val:Input's "left" and "right"
    /// keeps them apart; the others feed both sides from one channel.
    void process(const float* input, float* output, int numSamples) noexcept;
    void process(const float* input, float* outputL, float* outputR, int numSamples) noexcept;
    void process(const float* inputL, const float* inputR,
                 float* outputL, float* outputR, int numSamples) noexcept;

    /// Audio thread, after process(). The note events the circuit produced
    /// during the block just run, in the order they were emitted, each located
    /// within that block. Cleared at the start of every process().
    int outputEventCount() const noexcept { return numOutputEvents; }
    const ElementEvent& outputEvent(int index) const noexcept
    {
        return outputEvents[static_cast<std::size_t>(index)];
    }

    /// Message thread. Frees graphs the audio thread has finished with. Safe to
    /// call at any time; cheap when there is nothing to free.
    void collectGarbage();

    /// Message thread. True once a circuit is installed.
    bool hasCircuit() const { return active.load(std::memory_order_acquire) != nullptr; }

    /// Latency of the installed circuit, in samples.
    int latencyInSamples() const { return reportedLatency.load(std::memory_order_relaxed); }

    /// Message thread. Overrides one control input, as a host parameter does.
    /// Ignored if the node or port does not exist.
    void setControl(const std::string& nodeId, const std::string& portSymbol, float value);

    /// The value currently in use, which is the circuit's declared value until
    /// something overrides it. Returns nullopt if nothing has been set for this
    /// port, so the caller can fall back to the ontology or the model.
    std::optional<float> getControl(const std::string& nodeId,
                                    const std::string& portSymbol) const;

    /// Message thread. Reads the current value of a control output port (e.g. an
    /// Oscilloscope's peak, rms, or frequency) from the active graph's control
    /// store. Returns nullopt if the node or port is not found. A plain float
    /// read from the store — acceptable for meter display; no lock required.
    std::optional<float> getControlOutput(const std::string& nodeId,
                                          const std::string& portSymbol) const;

    /// Message thread. Ids of the nodes carrying a waveform tap
    /// (val:Oscilloscope and val:FreqAnalyzer), in circuit order.
    std::vector<std::string> tapNodes() const;

    /// Message thread. Copies the most recent samples observed at the node's
    /// audio output into `dest` (at most `maxSamples`) and returns how many
    /// were copied. Returns 0 when the node has no tap or nothing has run yet.
    /// The audio thread may be writing concurrently; a torn frame is acceptable
    /// for display, and every index stays inside the ring by construction.
    int readTap(const std::string& nodeId, float* dest, int maxSamples) const;

    /// The sample rate the engine is running at, for display scaling.
    /// Written by prepare() on the message thread; read-only afterwards.
    double currentSampleRate() const { return sampleRate; }

private:
    /// Everything one circuit needs, allocated together and freed together.
    struct Graph
    {
        CompiledCircuit circuit;
        std::vector<std::unique_ptr<DspElement>> elements;

        /// Flat buffer store: numBuffers blocks of maxBlockSize, contiguous.
        std::vector<float> audio;
        std::vector<float> controlStore;

        /// Scratch, sized once so process() never resizes.
        std::vector<const float*> audioInPtrs;
        std::vector<float*>       audioOutPtrs;
        std::vector<float>        controlIn;
        std::vector<float>        controlOut;

        /// Live control values, starting from the compiled defaults and
        /// overridden by setControl and by control arcs.
        std::vector<std::vector<float>> controlValues;
        std::array<float, 128> currentNoteVelocities{};

        int blockSize = 0;

        float* buffer(int index) noexcept
        {
            return audio.data() + static_cast<std::size_t>(index) * static_cast<std::size_t>(blockSize);
        }

        /// Waveform tap on one node's audio output. The ring is written by the
        /// audio thread and read by the message thread; `written` counts every
        /// sample ever stored, so readers derive bounded indices from it.
        /// Heap-held because the counter is atomic and therefore not movable.
        struct Tap
        {
            std::string nodeId;
            int nodeIndex = -1;
            int bufferIndex = -1;
            std::vector<float> ring;
            std::atomic<std::uint64_t> written{0};
        };

        /// One entry per Oscilloscope/FreqAnalyzer node, created at load().
        std::vector<std::unique_ptr<Tap>> taps;
        /// Per node, the index into `taps`, or -1. Sized alongside circuit.nodes.
        std::vector<int> nodeTap;
    };

    void processSlice(Graph&, const TransportInfo& sliceTransport,
                      const float* inputL, const float* inputR,
                      float* outputL, float* outputR, int numSamples,
                      int sliceOffset) noexcept;
    void retire(Graph* graph);

    /// Control values are recomputed on this grid, aligned to stream position
    /// rather than to host block boundaries, so the circuit sounds the same
    /// whatever buffer size the host chooses.
    static constexpr int kControlBlock = 32;

    /// Waveform tap ring capacity, in samples. At 48 kHz this holds ~85 ms:
    /// enough for a scope window and a 2048-point spectrum.
    static constexpr std::size_t kTapRingSize = 4096;

    std::atomic<Graph*> active{nullptr};
    std::atomic<std::uint64_t> blockCounter{0};
    std::uint64_t streamPosition = 0;   ///< audio thread only

    /// One voice of a circuit's pool: the note it is sounding and whether it is
    /// still held. Owned by the audio thread.
    ///
    /// A voice is not free the moment its note is released, because whatever is
    /// in it still has a release tail to finish. Allocation therefore prefers a
    /// voice that never started, then the longest-released one, then the oldest
    /// still held; every case is decided by `startedAt`, so the choice is
    /// deterministic and the same circuit renders the same way every time.
    struct Voice
    {
        bool  started  = false;
        bool  gate     = false;
        int   note     = 60;
        float velocity = 0.0f;
        std::uint64_t startedAt = 0;
    };

    static constexpr int kMaxVoices = 16;

    std::array<Voice, kMaxVoices> voices{};
    int numVoices = 0;            ///< how many of them this circuit uses
    std::uint64_t voiceClock = 0; ///< orders allocations, never wraps in practice

    int  allocateVoice(int note, float velocity) noexcept;
    void releaseVoice(int note) noexcept;

    /// Note state, owned by the audio thread.
    int   heldNotes      = 0;
    int   lastNoteNumber = 69;
    float lastVelocity   = 0.0f;
    bool  gate           = false;

    /// One note event located within the block about to run.
    struct NoteEvent
    {
        enum class Kind : std::uint8_t { On, Off, AllOff };

        int   sampleOffset = 0;
        Kind  kind         = Kind::On;
        int   note         = 0;
        float velocity     = 0.0f;
    };

    /// A block's worth of events, preallocated. A host that sends more than
    /// this in one block loses the excess, which is preferable to allocating.
    static constexpr int kMaxEventsPerBlock = 64;

    std::array<NoteEvent, kMaxEventsPerBlock> events{};
    int numEvents = 0;

    /// Events the circuit produced, preallocated for the same reason the input
    /// queue is: a block that produces more than this loses the excess rather
    /// than allocating on the audio thread.
    static constexpr int kMaxOutputEventsPerBlock = 64;

    std::array<ElementEvent, kMaxOutputEventsPerBlock> outputEvents{};
    int numOutputEvents = 0;

    void queueEvent(const NoteEvent&) noexcept;
    void applyEvent(const NoteEvent&) noexcept;

    /// Written by setTransport on the audio thread, read by process() there.
    TransportInfo transport;

    std::array<float, 128> activeNoteVelocities{};
    std::array<float, 128> triggeredNoteVelocities{};
    std::atomic<int> reportedLatency{0};

    struct Retired { Graph* graph; std::uint64_t atBlock; };
    std::vector<Retired> graveyard;   ///< message thread only

    double sampleRate = 44100.0;
    int maxBlockSize  = 512;

    /// Pending control overrides, applied to the next graph that loads.
    std::vector<std::tuple<std::string, std::string, float>> pendingControls;
};

}  // namespace valis

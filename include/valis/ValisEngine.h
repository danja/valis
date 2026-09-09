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
    /// oscillators can respond to. Bounded: beyond kMaxNotesPerBlock the extra
    /// events are dropped rather than allocating, which is the right trade on
    /// this thread.
    void noteOn(int noteNumber, float velocity) noexcept;
    void noteOff(int noteNumber) noexcept;
    void allNotesOff() noexcept;

    /// Audio thread. `input` may be null when the host gives us no input.
    void process(const float* input, float* output, int numSamples) noexcept;
    void process(const float* input, float* outputL, float* outputR, int numSamples) noexcept;

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

    void processSlice(Graph&, const float* input, float* outputL, float* outputR, int numSamples) noexcept;
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

    /// Note state, owned by the audio thread.
    int   heldNotes      = 0;
    int   lastNoteNumber = 69;
    float lastVelocity   = 0.0f;
    bool  gate           = false;
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

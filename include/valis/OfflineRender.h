// include/valis/OfflineRender.h
//
// Runs a circuit file offline: a private model, compiler and engine, optionally played by a MIDI file or fed a sound
// file, as valis-render does, but as a function so an operation can call it. It reads nothing live, so it may run on
// any thread; the ontology and the registry it is given are only read.

#pragma once

#include "valis/AudioFile.h"
#include "valis/CircuitModel.h"
#include "valis/MidiScore.h"

#include <string>
#include <vector>

namespace valis {

class Ontology;
class ElementRegistry;
class ValisEngine;

/// The block loop every offline render shares (valis-render, the render operation, a song's stems): plays `score` into
/// an engine that is already loaded and prepared at `sampleRate`, with `input` as its audio input, and fills `out` with
/// as many frames as `input` has. An event fires in the block that contains it, at its own offset inside the block, and a
/// host timeline is reported one position per block, so a transport-driven circuit renders as it plays. `score` must be
/// sorted by sample. The engine, not this function, owns what the circuit does with them.
struct BlockRun
{
    double sampleRate = 48000.0;
    double tempo = 120.0;
    bool   rolling = false;     ///< report the transport as playing from zero
    int    blockSize = 512;
};

void renderBlocks(ValisEngine& engine, const std::vector<float>& input, const std::vector<TimedNote>& score,
                  const BlockRun& run, StereoAudio& out);

struct OfflineRenderRequest
{
    std::string circuitPath;
    std::string midiPath;       ///< every note of this file is played at `tempo`; empty for none
    std::string inputPath;      ///< a sound file fed to the circuit (as mono); the render is as long as it is
    double seconds = 2.0;       ///< length when there is no input file
    double sampleRate = 48000.0;
    double tempo = 120.0;
    bool   rolling = false;     ///< report the transport as playing from zero
    int    blockSize = 512;
};

struct OfflineRenderResult
{
    bool ok = false;
    std::string error;                  ///< why it failed, with the file named
    std::vector<Diagnostic> diagnostics;
    StereoAudio audio;
    std::size_t nodes = 0;
    double peak = 0.0, rms = 0.0;       ///< of the mono sum of the two sides
    std::size_t nonFinite = 0;          ///< samples that were NaN or infinite; the stem is unusable if any
};

OfflineRenderResult renderCircuitFile(const Ontology& ontology, const ElementRegistry& registry,
                                      const OfflineRenderRequest& request);

} // namespace valis

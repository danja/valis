// include/valis/OfflineRender.h
//
// Runs a circuit file offline: a private model, compiler and engine, optionally played by a MIDI file or fed a sound
// file, as valis-render does, but as a function so an operation can call it. It reads nothing live, so it may run on
// any thread; the ontology and the registry it is given are only read.

#pragma once

#include "valis/AudioFile.h"
#include "valis/CircuitModel.h"

#include <string>
#include <vector>

namespace valis {

class Ontology;
class ElementRegistry;

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

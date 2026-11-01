// include/valis/SongRender.h
//
// Renders the stems of a song: every part is its circuit played by its MIDI file at the song's tempo with the
// transport rolling, and every return is its source part's stem run through the return's circuit. One wav per stem,
// `<name>.wav`, in the wav directory. Each render is a private model, compiler and engine (see OfflineRender.h), so
// several run at once and none touches a live circuit.

#pragma once

#include "valis/Song.h"

#include <string>
#include <vector>

namespace valis {

class Ontology;
class ElementRegistry;

struct SongRenderOptions
{
    std::vector<std::string> only;      ///< part names to render; empty for all. A return is redone with its source.
    int    workers = 3;                 ///< renders at once
    double sampleRate = 48000.0;
};

struct SongRenderEntry
{
    std::string name;
    bool   ok = false;
    std::string error;                  ///< why it was not rendered, with the file named
    std::string path;                   ///< the wav written
    double seconds = 0.0, peak = 0.0, rms = 0.0;
    bool   clipped = false;             ///< the peak passed full scale
};

struct SongRenderResult
{
    bool ok = false;                    ///< every stem asked for was rendered
    std::vector<SongRenderEntry> stems; ///< parts, then returns, as in the song
};

/// `songDirectory` is what the song's relative paths are read against. Fails a stem, not the whole song, when its
/// circuit or MIDI file is missing, its circuit will not load, or its audio holds a NaN; the others still render.
SongRenderResult renderSongStems(const Ontology& ontology, const ElementRegistry& registry, const Song& song,
                                 const std::string& songDirectory, const std::string& wavDirectory,
                                 const SongRenderOptions& options);

} // namespace valis

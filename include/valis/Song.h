// include/valis/Song.h
//
// A song as data: its tempo, its sections in order, the parts that play it and the effect returns that follow some
// of them. Read from a Turtle file (see val:Song in vocabs/valis.ttl and docs/songs.md). It holds no notes; those
// stay in the MIDI files each part names.
#pragma once

#include "valis/CircuitModel.h"
#include "valis/TurtleStore.h"

#include <optional>
#include <string>
#include <vector>

namespace valis {

struct SongSection
{
    std::string id, name;       ///< the IRI and its local name
    std::string label;          ///< rdfs:label, else the name
    int    index = 0;           ///< 1-based position
    int    bars = 0;
    double level = 0.0;         ///< dB applied to the mix while it plays
    int    firstBar = 0;        ///< 0-based bar the section starts on
};

/// A part or a return. Both are mixed as a stem called `name`.
struct SongStem
{
    std::string id, name;
    bool        isReturn = false;
    std::string circuit;        ///< relative to the song file's directory, or absolute
    std::string midi;           ///< a part's MIDI file; empty for a return
    std::string source;         ///< a return's source part, by name
    double level = -20.0;       ///< dB of the stem's playing rms
    double pan = 0.0;           ///< -1 to 1
    double highpass = 0.0;      ///< Hz, 0 for none
};

struct Song
{
    std::string id, title;
    double tempo = 120.0;
    int    beatsPerBar = 4;
    std::optional<double> loudness;     ///< LUFS the mix is aimed at
    double tailSeconds = 10.0;
    std::vector<SongSection> sections;  ///< in order
    std::vector<SongStem> stems;        ///< parts first, then returns; each group in index order, then by name

    int    totalBars() const;
    double barSeconds() const { return beatsPerBar * 60.0 / tempo; }
    double seconds() const { return totalBars() * barSeconds(); }              ///< without the tail
    double renderSeconds() const { return seconds() + tailSeconds; }

    /// The section a 0-based bar is in, and how far into it, or nullopt past the end.
    struct Place { int section; int barInSection; };
    std::optional<Place> locate(int bar) const;

    const SongStem* findStem(const std::string& name) const;

    /// Reads the one val:Song in `store`. Returns false, with a located message for each problem, if the file is
    /// not a usable song: no song or more than one, a tempo out of range, no sections, a gap in the section
    /// numbers, a part with no circuit, a pan out of range, a return that names no part, two stems of one name.
    static bool read(const rdf::TurtleStore& store, Song& song, std::vector<Diagnostic>& diagnostics);
};

} // namespace valis

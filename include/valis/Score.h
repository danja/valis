// include/valis/Score.h
//
// The notes of a song as data: patterns written as strings of steps, chords with their voicings, a progression, and a
// plan saying which part plays which pattern in which section. Read from a Turtle file named by the song's `val:score`
// (see val:Score in vocabs/valis.ttl and docs/songs.md). `ScoreWrite.h` turns it into one MIDI file per part.
#pragma once

#include "valis/CircuitModel.h"
#include "valis/Song.h"
#include "valis/TurtleStore.h"

#include <map>
#include <string>
#include <vector>

namespace valis {

/// One row of a pattern: a key, or a voice taken from the current chord, and the steps it sounds on.
struct ScoreLane
{
    int         key = -1;           ///< a MIDI key, or -1 when `voice` is used
    std::string voice;              ///< "chord" (every tone), "root", "pickup", "top" or "low", from the chord of the bar
    std::string steps;              ///< `x` a hit, `X` an accented hit, `.` or `-` a rest; spaces and `|` are ignored
    int    velocity = 90;
    int    accent = 20;             ///< added to the velocity on `X`
    double length = 1.0;            ///< in steps
    double gate = 0.8;              ///< the fraction of `length` the note is held, so a gate drops before the next note
    int    octave = 0;              ///< voices only
    int    transpose = 0;           ///< semitones
};

struct ScorePattern
{
    std::string name;
    int bars = 1;
    int stepsPerBeat = 4;           ///< 4 is sixteenth notes in a bar of quarter-note beats
    std::vector<ScoreLane> lanes;
};

struct ScoreChord
{
    std::string name;
    int root = 36;                  ///< the key a bass plays
    int pickup = -1;                ///< a second bass key, the approach to the next chord; the root if not given
    std::vector<int> tones;         ///< the voicing, as keys, lowest first
};

/// How a part is played: the looseness of a human and a shuffle. Without one a part is exact.
struct ScoreFeel
{
    std::string part;
    double timingMs = 0.0;          ///< standard deviation of the start time
    double velocity = 0.0;          ///< standard deviation of the velocity
    double swing = 0.5;             ///< where the second step of each pair falls, 0.5 straight
};

struct ScorePlay
{
    std::string section, part, pattern;
    int fromBar = 1;                ///< within the section, from 1
    int toBar = 0;                  ///< 0 for the end of the section
    int every = 0;                  ///< the pattern restarts every this many bars; 0 for its own length
    std::string progression;        ///< chord names for this play only, else the score's
};

struct Score
{
    std::string id;
    long long seed = 1;
    std::vector<std::string> progression;       ///< chord names, one for each `barsPerChord` bars
    int barsPerChord = 1;
    std::map<std::string, ScoreChord> chords;
    std::map<std::string, ScorePattern> patterns;
    std::vector<ScoreFeel> feels;
    std::vector<ScorePlay> plays;

    const ScoreFeel* feelOf(const std::string& part) const;

    /// Reads the one val:Score in `store`, checking it against `song`: every section and part it names must be in the
    /// song, every pattern and chord it uses must exist, and a pattern's steps must fill its bars exactly.
    static bool read(const rdf::TurtleStore& store, const Song& song, Score& score, std::vector<Diagnostic>& diagnostics);
};

} // namespace valis

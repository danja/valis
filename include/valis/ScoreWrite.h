// include/valis/ScoreWrite.h
//
// Plays a score into notes and writes one MIDI file per part. The same score and seed always give the same notes on every
// platform: the looseness of a human feel comes from a small generator defined here, not the standard library's. Reads
// and writes files only.
#pragma once

#include "valis/Score.h"
#include "valis/Song.h"

#include <string>
#include <vector>

namespace valis {

struct ScoreNote
{
    double beat = 0.0;          ///< start, in beats from the first bar
    double beats = 0.0;         ///< length
    int    key = 60;
    int    velocity = 90;
};

/// Every note of `part`, sorted by start then key. False, with the reason, if the part has no plays.
bool expandScorePart(const Song& song, const Score& score, const std::string& part, std::vector<ScoreNote>& notes,
                     std::string& error);

struct ScoreWriteEntry
{
    std::string part, path;
    int notes = 0;
};

struct ScoreWriteResult
{
    bool ok = false;
    std::string error;
    std::vector<ScoreWriteEntry> parts;
};

/// Writes `<songDirectory>/<part's val:midi>` for each part the score plays (all of them, or those named in `only`). A part
/// the score does not play is left alone, so its notes can still come from a script.
ScoreWriteResult writeScoreMidi(const Song& song, const Score& score, const std::string& songDirectory,
                                const std::vector<std::string>& only = {});

} // namespace valis

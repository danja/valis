// include/valis/SongCheck.h
//
// Checks a song's rendered stems against its score, so a stem that came out silent, clipped or wrong is found before
// the mix instead of by ear. Per part it counts the notes in the MIDI file for each bar and measures the stem's level
// in the same bar. Reads files only.

#pragma once

#include "valis/Song.h"

#include <string>
#include <vector>

namespace valis {

struct BarCheck
{
    int    bar = 0;             ///< from 1
    std::string section;
    int    notes = 0;           ///< note-ons in the MIDI file that start in this bar
    double rmsDb = -120.0;      ///< of the stem over the bar, both channels
};

struct StemReport
{
    std::string name;
    bool   isReturn = false;
    bool   found = false;       ///< the wav was read
    bool   hasMidi = false;     ///< a MIDI file was read (parts, and returns that name one)
    int    notes = 0;
    double peak = 0.0, rmsDb = -120.0;
    bool   nonFinite = false, clipped = false;
    std::vector<int> silentBars;    ///< bars with notes starting in them and a level under the silence threshold
    std::vector<std::string> problems;
    std::vector<BarCheck> bars;
};

struct SongCheckResult
{
    bool ok = false;            ///< no stem has a problem
    std::vector<StemReport> stems;
};

/// `silenceDb` is the level under which a bar counts as silent (-70 dBFS rms by default).
SongCheckResult checkSongStems(const Song& song, const std::string& songDirectory, const std::string& wavDirectory,
                               double silenceDb = -70.0);

} // namespace valis

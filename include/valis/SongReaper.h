// include/valis/SongReaper.h
//
// Writes a Reaper project (https://www.reaper.fm) from a song file: one track per rendered stem, the stem's wav placed
// at the start, the track's volume and pan set to what the song's mix gives that stem, and the project's tempo and meter
// set from the song. The project is a starting point for mixing by ear, built from the same levels the reference mix uses.
// The high-pass is not written. Reads and writes files only.

#pragma once

#include "valis/Song.h"

#include <string>
#include <vector>

namespace valis {

struct ReaperTrack
{
    std::string name;
    double volumeDb = 0.0;      ///< relative to the loudest stem, which is 0
    double pan = 0.0;
    bool   muted = false;       ///< a val:silent part, kept so the return it feeds can be rebuilt
    double seconds = 0.0;
};

struct ReaperResult
{
    bool ok = false;
    std::string error;
    std::string outputPath;
    std::vector<ReaperTrack> tracks;
    std::vector<std::string> missing;   ///< stems with no wav; they get no track
};

/// Reads each `<wavDirectory>/<stem>.wav`, works out the mix's gain for each, and writes the project to `outputPath`.
/// Refuses to replace a file there unless `overwrite`, since the project may have been edited in Reaper.
ReaperResult writeReaperProject(const Song& song, const std::string& wavDirectory, const std::string& outputPath,
                                bool overwrite);

} // namespace valis

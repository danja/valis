// include/valis/MidiScore.h
//
// The notes of a standard MIDI file placed on a sample timeline, shared by valis-render and the render operation.
// Message thread only: it reads a file.

#pragma once

#include <string>
#include <vector>

namespace valis {

/// One note event, placed by stream position in samples.
struct TimedNote
{
    long long sample   = 0;
    bool      on       = false;
    int       note     = 0;
    float     velocity = 0.0f;
    int       controller = -1;  ///< 0 to 127 for a controller change, whose value is `velocity` (0 to 1); -1 for a note
};

/// Reads every note of a MIDI file at a fixed tempo, as a host playing at that tempo would place them, sorted by
/// time. Tempo events in the file are ignored. A relative path is read against the working directory. On failure
/// returns false and writes the reason into `error`.
/// With `controllers` true, controller changes are returned too, as events with `controller` set; a caller that treats
/// every event as a note must leave it false.
bool readMidiScore(const std::string& path, double tempoBpm, double sampleRate,
                   std::vector<TimedNote>& notes, std::string& error, bool controllers = false);

}  // namespace valis

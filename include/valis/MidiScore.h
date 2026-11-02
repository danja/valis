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
    int       channel  = 1;     ///< 1 to 16; only the notes a circuit sends out carry one, a file's are all played alike
};

/// Reads every note of a MIDI file at a fixed tempo, as a host playing at that tempo would place them, sorted by
/// time. Tempo events in the file are ignored. A relative path is read against the working directory. On failure
/// returns false and writes the reason into `error`.
/// With `controllers` true, controller changes are returned too, as events with `controller` set; a caller that treats
/// every event as a note must leave it false.
bool readMidiScore(const std::string& path, double tempoBpm, double sampleRate,
                   std::vector<TimedNote>& notes, std::string& error, bool controllers = false);

/// Writes notes as a type 1 standard MIDI file with one track, at a fixed tempo, so `sample` becomes the same position
/// in beats that readMidiScore would read it back from. Controller changes are written too. A relative path is
/// written against the working directory. On failure returns false and writes the reason into `error`.
bool writeMidiScore(const std::string& path, const std::vector<TimedNote>& notes, double tempoBpm, double sampleRate,
                    std::string& error);

}  // namespace valis

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
};

/// Reads every note of a MIDI file at a fixed tempo, as a host playing at that tempo would place them, sorted by
/// time. Tempo events in the file are ignored. A relative path is read against the working directory. On failure
/// returns false and writes the reason into `error`.
bool readMidiScore(const std::string& path, double tempoBpm, double sampleRate,
                   std::vector<TimedNote>& notes, std::string& error);

}  // namespace valis

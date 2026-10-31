// src/ops/MidiScore.cpp

#include "valis/MidiScore.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>

namespace valis {

bool readMidiScore(const std::string& path, double tempoBpm, double sampleRate,
                   std::vector<TimedNote>& notes, std::string& error)
{
    if (tempoBpm <= 0.0)
    {
        error = "tempo must be above zero";
        return false;
    }

    juce::MidiFile file;
    juce::FileInputStream in{juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(path))};
    if (! in.openedOk() || ! file.readFrom(in))
    {
        error = "cannot read MIDI file " + path;
        return false;
    }

    const int division = file.getTimeFormat();
    if (division <= 0)
    {
        error = path + ": SMPTE time is not supported";
        return false;
    }

    for (int t = 0; t < file.getNumTracks(); ++t)
    {
        const auto* sequence = file.getTrack(t);
        for (int i = 0; i < sequence->getNumEvents(); ++i)
        {
            const auto& message = sequence->getEventPointer(i)->message;
            if (! message.isNoteOnOrOff())
                continue;

            const double quarters = message.getTimeStamp() / static_cast<double>(division);
            TimedNote note;
            note.sample   = static_cast<long long>(quarters * 60.0 / tempoBpm * sampleRate);
            note.on       = message.isNoteOn();
            note.note     = message.getNoteNumber();
            note.velocity = message.getFloatVelocity();
            notes.push_back(note);
        }
    }

    std::stable_sort(notes.begin(), notes.end(),
                     [](const TimedNote& a, const TimedNote& b) { return a.sample < b.sample; });
    return true;
}

}  // namespace valis

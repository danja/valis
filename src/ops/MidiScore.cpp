// src/ops/MidiScore.cpp

#include "valis/MidiScore.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>

namespace valis {

bool readMidiScore(const std::string& path, double tempoBpm, double sampleRate,
                   std::vector<TimedNote>& notes, std::string& error, bool controllers)
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
            const bool isController = controllers && message.isController();
            if (! message.isNoteOnOrOff() && ! isController)
                continue;

            const double quarters = message.getTimeStamp() / static_cast<double>(division);
            TimedNote note;
            if (isController)
            {
                note.sample     = static_cast<long long>(quarters * 60.0 / tempoBpm * sampleRate);
                note.controller = message.getControllerNumber();
                note.velocity   = static_cast<float>(message.getControllerValue()) / 127.0f;
                notes.push_back(note);
                continue;
            }
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

bool writeMidiScore(const std::string& path, const std::vector<TimedNote>& notes, double tempoBpm, double sampleRate,
                    std::string& error)
{
    if (tempoBpm <= 0.0 || sampleRate <= 0.0)
    {
        error = "tempo and sample rate must be above zero";
        return false;
    }

    constexpr int division = 960;
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / tempoBpm)), 0.0);

    for (const auto& n : notes)
    {
        const double ticks = static_cast<double>(n.sample) / sampleRate * tempoBpm / 60.0 * division;
        const int channel = std::clamp(n.channel, 1, 16);
        if (n.controller >= 0)
            track.addEvent(juce::MidiMessage::controllerEvent(channel, std::clamp(n.controller, 0, 127),
                                                              static_cast<int>(std::lround(std::clamp(n.velocity, 0.0f, 1.0f) * 127.0f))),
                           ticks);
        else if (n.on)
            track.addEvent(juce::MidiMessage::noteOn(channel, std::clamp(n.note, 0, 127), n.velocity), ticks);
        else
            track.addEvent(juce::MidiMessage::noteOff(channel, std::clamp(n.note, 0, 127)), ticks);
    }
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote(division);
    file.addTrack(track);

    const auto target = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(path));
    target.deleteFile();
    juce::FileOutputStream out(target);
    if (! out.openedOk() || ! file.writeTo(out))
    {
        error = "cannot write MIDI file " + path;
        return false;
    }
    return true;
}

}  // namespace valis

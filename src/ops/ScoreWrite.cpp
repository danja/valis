// src/ops/ScoreWrite.cpp
#include "valis/ScoreWrite.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <sstream>

namespace valis {

namespace {

// splitmix64: a fixed, portable generator. A Gaussian is the sum of twelve uniforms less six.
class Random
{
public:
    explicit Random(std::uint64_t seed) : state(seed) {}

    double uniform()
    {
        state += 0x9e3779b97f4a7c15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) / 9007199254740992.0;
    }

    double gaussian()
    {
        double sum = 0.0;
        for (int i = 0; i < 12; ++i) sum += uniform();
        return sum - 6.0;
    }

private:
    std::uint64_t state;
};

std::uint64_t hashName(const std::string& s)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (const char c : s)
    {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

std::vector<std::string> words(const std::string& text)
{
    std::istringstream in(text);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

int clampKey(int k) { return std::clamp(k, 0, 127); }

} // namespace

bool expandScorePart(const Song& song, const Score& score, const std::string& part, std::vector<ScoreNote>& notes,
                     std::string& error)
{
    notes.clear();
    bool played = false;
    Random random(static_cast<std::uint64_t>(score.seed) * 0x9e3779b97f4a7c15ULL ^ hashName(part));
    const auto* feel = score.feelOf(part);
    const double beatMs = 60000.0 / song.tempo;

    for (const auto& play : score.plays)
    {
        if (play.part != part) continue;
        played = true;

        const SongSection* section = nullptr;
        for (const auto& s : song.sections)
            if (s.name == play.section) section = &s;
        const auto& pattern = score.patterns.at(play.pattern);
        const int last = play.toBar == 0 ? section->bars : play.toBar;
        const int every = play.every > 0 ? play.every : pattern.bars;
        const auto chordNames = play.progression.empty() ? score.progression : words(play.progression);
        const int stepsPerBar = song.beatsPerBar * pattern.stepsPerBeat;

        for (int bar = play.fromBar; bar <= last; ++bar)
        {
            const int into = (bar - play.fromBar) % every;     // bars since the pattern last restarted
            if (into >= pattern.bars) continue;

            const int barInSection = bar - 1;
            const double barBeat = static_cast<double>(section->firstBar + barInSection) * song.beatsPerBar;
            const ScoreChord* chord = nullptr;
            if (! chordNames.empty())
            {
                const auto index = static_cast<std::size_t>(barInSection / score.barsPerChord) % chordNames.size();
                chord = &score.chords.at(chordNames[index]);
            }

            for (const auto& lane : pattern.lanes)
            {
                for (int s = 0; s < stepsPerBar; ++s)
                {
                    const char c = lane.steps[static_cast<std::size_t>(into * stepsPerBar + s)];
                    if (c != 'x' && c != 'X') continue;

                    // where the step falls: straight, or the second of each pair pushed late by the swing
                    const double stepBeats = 1.0 / pattern.stepsPerBeat;
                    double start = barBeat + s * stepBeats;
                    if (feel != nullptr && feel->swing > 0.5 && (s % 2) == 1)
                        start = barBeat + (s - 1) * stepBeats + feel->swing * 2.0 * stepBeats;

                    std::vector<int> keys;
                    if (lane.key >= 0)
                        keys.push_back(lane.key + lane.transpose);
                    else if (chord != nullptr)
                    {
                        if (lane.voice == "chord")
                            for (const int t : chord->tones) keys.push_back(t + 12 * lane.octave + lane.transpose);
                        else if (lane.voice == "root")
                            keys.push_back(chord->root + 12 * lane.octave + lane.transpose);
                        else if (lane.voice == "top")
                            keys.push_back(chord->tones.back() + 12 * lane.octave + lane.transpose);
                        else
                            keys.push_back(chord->tones.front() + 12 * lane.octave + lane.transpose);
                    }

                    for (const int key : keys)
                    {
                        ScoreNote n;
                        n.key = clampKey(key);
                        n.beats = lane.length * stepBeats * lane.gate;
                        double velocity = lane.velocity + (c == 'X' ? lane.accent : 0);
                        n.beat = start;
                        if (feel != nullptr)
                        {
                            n.beat = std::max(0.0, start + random.gaussian() * feel->timingMs / beatMs);
                            velocity += random.gaussian() * feel->velocity;
                        }
                        n.velocity = std::clamp(static_cast<int>(std::lround(velocity)), 1, 127);
                        notes.push_back(n);
                    }
                }
            }
        }
    }

    if (! played)
    {
        error = "the score does not play the part '" + part + "'";
        return false;
    }
    std::stable_sort(notes.begin(), notes.end(), [](const ScoreNote& a, const ScoreNote& b)
                     { return a.beat < b.beat || (a.beat == b.beat && a.key < b.key); });
    return true;
}

ScoreWriteResult writeScoreMidi(const Song& song, const Score& score, const std::string& songDirectory,
                                const std::vector<std::string>& only)
{
    ScoreWriteResult result;
    std::set<std::string> parts;
    for (const auto& p : score.plays) parts.insert(p.part);
    for (const auto& name : only)
        if (parts.count(name) == 0)
        {
            result.error = "the score does not play the part '" + name + "'";
            return result;
        }

    constexpr int ppq = 480;
    for (const auto& name : parts)
    {
        if (! only.empty() && std::find(only.begin(), only.end(), name) == only.end()) continue;

        const auto* stem = song.findStem(name);
        std::vector<ScoreNote> notes;
        if (! expandScorePart(song, score, name, notes, result.error))
            return result;

        juce::MidiMessageSequence sequence;
        sequence.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / song.tempo)), 0.0);
        for (const auto& n : notes)
        {
            const double on = std::round(n.beat * ppq), off = std::max(on + 1.0, std::round((n.beat + n.beats) * ppq));
            sequence.addEvent(juce::MidiMessage::noteOn(1, n.key, static_cast<juce::uint8>(n.velocity)), on);
            sequence.addEvent(juce::MidiMessage::noteOff(1, n.key), off);
        }
        sequence.sort();                       // offs sort before ons at a tick, so a repeated key restarts
        sequence.updateMatchedPairs();
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote(ppq);
        midi.addTrack(sequence);

        const juce::File path = stem->midi.front() == '/' ? juce::File(juce::String(stem->midi))
                                                          : juce::File(juce::String(songDirectory)).getChildFile(juce::String(stem->midi));
        path.getParentDirectory().createDirectory();
        path.deleteFile();
        juce::FileOutputStream out(path);
        if (! out.openedOk() || ! midi.writeTo(out))
        {
            result.error = "could not write " + path.getFullPathName().toStdString();
            return result;
        }
        result.parts.push_back({name, path.getFullPathName().toStdString(), static_cast<int>(notes.size())});
    }
    result.ok = true;
    return result;
}

} // namespace valis

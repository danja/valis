// src/ops/SongReaper.cpp
#include "valis/SongReaper.h"

#include "valis/AudioFile.h"
#include "valis/SongMix.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace valis {

namespace {

std::string number(double v)
{
    char text[40];
    std::snprintf(text, sizeof text, "%.6g", v);
    return text;
}

// A Reaper track needs an id of its own; this one is made from the track's place, so a project is repeatable.
std::string trackId(std::size_t index)
{
    char text[64];
    std::snprintf(text, sizeof text, "{00000000-0000-4000-8000-%012zu}", index + 1);
    return text;
}

} // namespace

ReaperResult writeReaperProject(const Song& song, const std::string& wavDirectory, const std::string& outputPath,
                                bool overwrite, bool midi, const std::string& songDirectory)
{
    ReaperResult result;
    const juce::File target{juce::String(outputPath)};
    if (target.existsAsFile() && ! overwrite)
    {
        result.error = outputPath + " exists; it may have been edited in Reaper, so it is not replaced (use overwrite)";
        return result;
    }

    std::vector<MixStem> stems;
    std::vector<const SongStem*> from;
    for (const auto& s : song.stems)
    {
        MixStem stem;
        std::string error;
        if (! readAudioFile(wavDirectory + "/" + s.name + ".wav", stem.audio, error))
        {
            result.missing.push_back(s.name);
            continue;
        }
        stem.name = s.name;
        stem.levelDb = s.level;
        stem.pan = s.pan;
        stem.highpassHz = s.highpass;
        stem.follows = s.follows;
        stems.push_back(std::move(stem));
        from.push_back(&s);
    }
    if (stems.empty() && ! midi)
    {
        result.error = "no stem wavs in " + wavDirectory + " (render the song first)";
        return result;
    }

    // The loudest stem sits at 0 dB, so no fader is above unity and the balance is the mix's.
    std::vector<double> gains;
    double rate = 48000.0, loudest = 0.0;
    if (! stems.empty())
    {
        rate = stems.front().audio.sampleRate;
        std::vector<MixStemReport> levels;
        if (! songmix::stemGains(stems, rate, gains, levels, result.error))
            return result;
        loudest = *std::max_element(gains.begin(), gains.end());
    }
    const auto volumeOf = [&](const std::string& name)
    {
        for (std::size_t k = 0; k < stems.size(); ++k)
            if (stems[k].name == name) return gains[k] - loudest;
        return 0.0;
    };

    if (! midi)
    {
        for (std::size_t k = 0; k < stems.size(); ++k)
        {
            ReaperTrack t;
            t.name = stems[k].name;
            t.volumeDb = gains[k] - loudest;
            t.pan = std::clamp(stems[k].pan, -1.0, 1.0);
            t.muted = from[k]->silent;
            t.seconds = static_cast<double>(stems[k].audio.frames()) / rate;
            t.circuit = from[k]->circuit;
            result.tracks.push_back(t);
        }
    }
    else
    {
        result.missing.clear();     // here a part is missing when its MIDI file is, not its wav
        for (const auto& s : song.stems)
        {
            if (s.isReturn)
                continue;
            const auto path = s.midi.empty() ? std::string() : (s.midi.front() == '/' ? s.midi : songDirectory + "/" + s.midi);
            if (path.empty() || ! juce::File(juce::String(path)).existsAsFile())
            {
                result.missing.push_back(s.name);
                continue;
            }
            ReaperTrack t;
            t.name = s.name;
            t.volumeDb = volumeOf(s.name);
            t.pan = std::clamp(s.pan, -1.0, 1.0);
            t.muted = s.silent;
            t.seconds = song.renderSeconds();
            t.midiPath = path;
            t.circuit = s.circuit;
            result.tracks.push_back(t);
        }
        if (result.tracks.empty())
        {
            result.error = "no MIDI files found for the parts (run valis-song score, or the song's score script)";
            return result;
        }
    }

    std::string out = "<REAPER_PROJECT 0.1 \"7.83/linux-x86_64\" 1793285344 0\n";
    out += "  TEMPO " + number(song.tempo) + " " + std::to_string(song.beatsPerBar) + " 4\n";
    out += "  SAMPLERATE " + number(rate) + " 0 0\n";
    for (std::size_t k = 0; k < result.tracks.size(); ++k)
    {
        const auto& t = result.tracks[k];
        out += "  <TRACK\n";
        out += "    NAME \"" + t.name + "\"\n";
        out += "    VOLPAN " + number(std::pow(10.0, t.volumeDb / 20.0)) + " " + number(t.pan) + " -1 -1 1\n";
        out += std::string("    MUTESOLO ") + (t.muted ? "1" : "0") + " 0 0\n";
        out += "    TRACKID " + trackId(k) + "\n";
        out += "    <ITEM\n";
        out += "      POSITION 0\n";
        out += "      LENGTH " + number(t.seconds) + "\n";
        out += "      LOOP 0\n";
        if (midi)
        {
            out += "      NAME \"" + t.name + ".mid\"\n";
            out += "      <SOURCE MIDI\n";
            out += "        FILE \"" + t.midiPath + "\"\n";
        }
        else
        {
            out += "      NAME \"" + t.name + ".wav\"\n";
            out += "      <SOURCE WAVE\n";
            out += "        FILE \"" + wavDirectory + "/" + t.name + ".wav\"\n";
        }
        out += "      >\n";
        out += "    >\n";
        out += "  >\n";
    }
    out += ">\n";

    if (! target.getParentDirectory().createDirectory() || ! target.replaceWithText(juce::String(out)))
    {
        result.error = "could not write " + outputPath;
        return result;
    }
    result.outputPath = outputPath;
    result.ok = true;
    return result;
}

} // namespace valis

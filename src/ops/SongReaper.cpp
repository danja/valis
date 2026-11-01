// src/ops/SongReaper.cpp
#include "valis/SongReaper.h"

#include "valis/AudioFile.h"
#include "valis/SongMix.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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


// The Valis plugin keeps its circuit and its MCP setting in the project, so a track can be written already
// loaded. Reaper wraps the plugin's state in a header of sizes (copied from a project Reaper saved), and
// the plugin's own state is JUCE's "VC2!" block: the size of an XML text, the text, then private data.
std::string xmlAttribute(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        switch (c)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\n': out += "&#10;"; break;
            case '\r': out += "&#13;"; break;
            case '\t': out += "&#9;"; break;
            default: out += c;
        }
    }
    return out;
}

void putU32(std::string& out, std::uint32_t v)
{
    for (int k = 0; k < 4; ++k)
        out += static_cast<char>((v >> (8 * k)) & 0xff);
}

// Standard base64, which Reaper reads. juce::Base64 uses an alphabet of its own.
std::string base64(const std::string& data)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t at = 0; at < data.size(); at += 3)
    {
        const auto left = std::min<std::size_t>(3, data.size() - at);
        std::uint32_t group = 0;
        for (std::size_t k = 0; k < 3; ++k)
            group = group << 8 | (k < left ? static_cast<unsigned char>(data[at + k]) : 0u);
        for (std::size_t k = 0; k < 4; ++k)
            out += k <= left ? kAlphabet[(group >> (18 - 6 * k)) & 63] : '=';
    }
    return out;
}

std::string valisChunk(const std::string& turtle, int mcpPort)
{
    static constexpr unsigned char kHead[48] = {
        0xfe, 0x58, 0x46, 0x34, 0xee, 0x5e, 0xed, 0xfe, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static constexpr unsigned char kTail[69] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x55, 0x43, 0x45, 0x50, 0x72, 0x69,
        0x76, 0x61, 0x74, 0x65, 0x44, 0x61, 0x74, 0x61, 0x00, 0x01, 0x01, 0x42, 0x79, 0x70, 0x61, 0x73,
        0x73, 0x00, 0x01, 0x01, 0x03, 0x00, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x55,
        0x43, 0x45, 0x50, 0x72, 0x69, 0x76, 0x61, 0x74, 0x65, 0x44, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00 };

    const std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?> <VALIS turtle=\"" + xmlAttribute(turtle)
        + "\" mcpEnabled=\"1\" mcpPort=\"" + std::to_string(mcpPort) + "\" samples=\"\"></VALIS>";

    const auto inner = static_cast<std::uint32_t>(xml.size() + sizeof kTail);

    std::string raw(reinterpret_cast<const char*>(kHead), sizeof kHead);
    putU32(raw, inner + 16);
    putU32(raw, 1);
    putU32(raw, 0xffff);
    putU32(raw, inner);
    putU32(raw, 1);
    raw += "VC2!";
    putU32(raw, static_cast<std::uint32_t>(xml.size()));
    raw += xml;
    raw.append(reinterpret_cast<const char*>(kTail), sizeof kTail);

    const auto encoded = base64(raw);
    std::string out;
    for (std::size_t at = 0; at < encoded.size(); at += 128)
        out += "        " + encoded.substr(at, 128) + "\n";
    return out;
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
        if (midi)
        {
            // The circuit plays the track's MIDI, so the instrument is written already loaded.
            const auto circuitPath = t.circuit.empty() ? std::string()
                : (t.circuit.front() == '/' ? t.circuit : songDirectory + "/" + t.circuit);
            const auto turtle = juce::File(juce::String(circuitPath)).loadFileAsString().toStdString();
            if (turtle.empty())
            {
                result.error = "could not read the circuit of part " + t.name + ": " + circuitPath;
                return result;
            }
            char fxId[64];
            std::snprintf(fxId, sizeof fxId, "{00000000-0000-4000-8000-F%011zu}", k + 1);
            out += "    FX 1\n";
            out += "    <FXCHAIN\n      SHOW 0\n      LASTSEL 0\n      DOCKED 0\n      BYPASS 0 0 0\n";
            out += "      <VST \"VST3: Valis (danja)\" Valis.vst3 0 \"\" 877025534{ABCDEF019182FAEB446E6A6156616C73} \"\"\n";
            out += valisChunk(turtle, 7677 + static_cast<int>(k));
            out += "      >\n      PRESETNAME Default\n      FXID " + std::string(fxId) + "\n      WAK 0 0\n    >\n";
        }
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

// src/dsp/SampleMap.h
//
// The sample table of val:Sampler: which sound plays for which key and velocity, and how. It is read and built
// on the message thread, where files may be opened; the audio thread only reads it.
//
// A table is text, one zone per line, each a list of key=value words:
//
//   file=audio/conga-d-sharp-4-373411.mp3 root=63 keys=60-65 vel=1-127 gain=-3 tune=0 choke=1
//
// A value with a space in it is put in double quotes. Blank lines and lines starting with # are ignored, and so is
// a ; as a line break, so a table fits on one line. Times are in seconds of the file.

#pragma once

#include "dsp/SampleFile.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace valis::dsp {

/// One sound and the part of the keyboard and the velocity range it answers.
struct SampleZone
{
    std::shared_ptr<const std::vector<float>> samples;   ///< mono, at the engine's rate
    std::string name;

    int    keyLow = 0, keyHigh = 127;
    int    root = 60;                  ///< the key at which the sound plays at its own pitch
    int    velocityLow = 1, velocityHigh = 127;
    float  gainDb = 0.0f;
    float  tune = 0.0f;                ///< semitones
    int    chokeGroup = 0;             ///< a hit cuts the other sounding hits of the same non-zero group
    bool   reverse = false;

    /// Positions in frames. loopEnd <= loopStart means no loop points were given.
    double start = 0.0, end = 0.0;
    double loopStart = 0.0, loopEnd = 0.0;
    double fade = 0.0;                 ///< length of the crossfade into the loop start

    int length() const noexcept { return samples ? static_cast<int>(samples->size()) : 0; }
    bool hasLoop() const noexcept { return loopEnd > loopStart; }
};

class SampleMap
{
public:
    std::vector<SampleZone> zones;

    bool empty() const noexcept { return zones.empty(); }

    /// Reads a table, loading each file once however many zones use it. On failure `error` says which line
    /// and what was wrong, and `zones` is left as it was.
    bool parse(const std::string& table, double rate, std::string& error)
    {
        std::map<std::string, std::shared_ptr<const std::vector<float>>> cache;
        std::vector<SampleZone> built;
        int lineNumber = 0;

        std::string text = table;
        std::replace(text.begin(), text.end(), ';', '\n');

        std::size_t from = 0;
        while (from <= text.size())
        {
            auto to = text.find('\n', from);
            if (to == std::string::npos) to = text.size();
            const auto line = trim(text.substr(from, to - from));
            from = to + 1;
            ++lineNumber;
            if (line.empty() || line[0] == '#')
                continue;

            SampleZone zone;
            if (! readZone(line, rate, cache, zone, error))
            {
                error = "sample map line " + std::to_string(lineNumber) + ": " + error;
                return false;
            }
            built.push_back(std::move(zone));
        }

        zones = std::move(built);
        return true;
    }

    /// The zones that answer a key and a velocity (0 to 1), in table order.
    /// A key with none falls back to the zone whose range is nearest, so a keyboard instrument made of a few
    /// samples plays everywhere and the nearest one is stretched; a velocity with none falls back to the
    /// nearest layer. Writes at most `capacity` indices and returns how many.
    int candidates(int key, float velocity, int* out, int capacity) const noexcept
    {
        const int vel = std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), 1, 127);
        int count = 0;
        for (std::size_t i = 0; i < zones.size() && count < capacity; ++i)
        {
            const auto& z = zones[i];
            if (key >= z.keyLow && key <= z.keyHigh && vel >= z.velocityLow && vel <= z.velocityHigh)
                out[count++] = static_cast<int>(i);
        }
        if (count > 0 || zones.empty())
            return count;

        // Nothing answers: take the zones nearest in key, then nearest in velocity.
        int bestDistance = 1 << 20;
        for (const auto& z : zones)
            bestDistance = std::min(bestDistance, keyDistance(z, key) * 128 + velocityDistance(z, vel));
        for (std::size_t i = 0; i < zones.size() && count < capacity; ++i)
            if (keyDistance(zones[i], key) * 128 + velocityDistance(zones[i], vel) == bestDistance)
                out[count++] = static_cast<int>(i);
        return count;
    }

private:
    static int keyDistance(const SampleZone& z, int key) noexcept
    {
        return key < z.keyLow ? z.keyLow - key : (key > z.keyHigh ? key - z.keyHigh : 0);
    }

    static int velocityDistance(const SampleZone& z, int vel) noexcept
    {
        return vel < z.velocityLow ? z.velocityLow - vel : (vel > z.velocityHigh ? vel - z.velocityHigh : 0);
    }

    static std::string trim(const std::string& s)
    {
        const auto a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos) return {};
        return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
    }

    /// Splits a line into key=value words, honouring double quotes around a value.
    static bool words(const std::string& line, std::vector<std::pair<std::string, std::string>>& out,
                      std::string& error)
    {
        std::size_t i = 0;
        while (i < line.size())
        {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            if (i >= line.size()) break;
            const auto eq = line.find('=', i);
            if (eq == std::string::npos)
            {
                error = "expected key=value, found '" + line.substr(i) + "'";
                return false;
            }
            const auto key = line.substr(i, eq - i);
            i = eq + 1;
            std::string value;
            if (i < line.size() && line[i] == '"')
            {
                const auto close = line.find('"', i + 1);
                if (close == std::string::npos)
                {
                    error = "unterminated quote after " + key + "=";
                    return false;
                }
                value = line.substr(i + 1, close - i - 1);
                i = close + 1;
            }
            else
            {
                const auto end = line.find_first_of(" \t", i);
                value = line.substr(i, end == std::string::npos ? std::string::npos : end - i);
                i = end == std::string::npos ? line.size() : end;
            }
            out.emplace_back(key, value);
        }
        return true;
    }

    static bool number(const std::string& key, const std::string& value, double& result, std::string& error)
    {
        try
        {
            std::size_t used = 0;
            result = std::stod(value, &used);
            if (used == value.size() && std::isfinite(result))
                return true;
        }
        catch (...) {}
        error = key + " must be a number, not '" + value + "'";
        return false;
    }

    /// "36" or "36-48".
    static bool range(const std::string& key, const std::string& value, int lowest, int highest,
                      int& low, int& high, std::string& error)
    {
        const auto dash = value.find('-', 1);
        double a = 0.0, b = 0.0;
        if (dash == std::string::npos)
        {
            if (! number(key, value, a, error)) return false;
            b = a;
        }
        else if (! number(key, value.substr(0, dash), a, error) || ! number(key, value.substr(dash + 1), b, error))
            return false;

        low = static_cast<int>(a);
        high = static_cast<int>(b);
        if (low > high || low < lowest || high > highest)
        {
            error = key + " must be a range inside " + std::to_string(lowest) + " to " + std::to_string(highest)
                  + ", low first, not '" + value + "'";
            return false;
        }
        return true;
    }

    bool readZone(const std::string& line, double rate,
                  std::map<std::string, std::shared_ptr<const std::vector<float>>>& cache,
                  SampleZone& zone, std::string& error)
    {
        std::vector<std::pair<std::string, std::string>> pairs;
        if (! words(line, pairs, error))
            return false;

        std::string path, expectedHash;
        double startSeconds = 0.0, endSeconds = -1.0, loopStartSeconds = -1.0, loopEndSeconds = -1.0, fadeSeconds = 0.0;
        double value = 0.0;
        bool rootGiven = false;

        for (const auto& [key, text] : pairs)
        {
            if (key == "file")        path = text;
            else if (key == "sha256") expectedHash = text;
            else if (key == "keys")   { if (! range(key, text, 0, 127, zone.keyLow, zone.keyHigh, error)) return false; }
            else if (key == "key")    { if (! range(key, text, 0, 127, zone.keyLow, zone.keyHigh, error)) return false; }
            else if (key == "vel")    { if (! range(key, text, 1, 127, zone.velocityLow, zone.velocityHigh, error)) return false; }
            else
            {
                static const char* const kNumeric[] = {"root", "gain", "tune", "choke", "reverse", "start", "end",
                                                       "loopStart", "loopEnd", "fade"};
                if (std::find(std::begin(kNumeric), std::end(kNumeric), key) == std::end(kNumeric))
                {
                    error = "unknown word '" + key + "'";
                    return false;
                }
                if (! number(key, text, value, error))
                    return false;

                if (key == "root")
                {
                    if (value < 0 || value > 127) { error = "root must be a MIDI note, 0 to 127"; return false; }
                    zone.root = static_cast<int>(std::lround(value));
                    rootGiven = true;
                }
                else if (key == "gain")       zone.gainDb = static_cast<float>(value);
                else if (key == "tune")       zone.tune = static_cast<float>(value);
                else if (key == "choke")      zone.chokeGroup = static_cast<int>(value);
                else if (key == "reverse")    zone.reverse = value > 0.5;
                else if (key == "start")      startSeconds = value;
                else if (key == "end")        endSeconds = value;
                else if (key == "loopStart")  loopStartSeconds = value;
                else if (key == "loopEnd")    loopEndSeconds = value;
                else if (key == "fade")       fadeSeconds = value;
            }
        }

        if (path.empty())
        {
            error = "a zone needs file=";
            return false;
        }

        // A single key with no root plays at its own pitch, which is what a drum pad wants.
        if (! rootGiven && zone.keyLow == zone.keyHigh)
            zone.root = zone.keyLow;

        auto found = cache.find(path);
        if (found == cache.end())
        {
            SampleFile file;
            if (! file.load(path, rate, error))
                return false;
            if (! expectedHash.empty() && juce::String(expectedHash).toLowerCase().toStdString() != file.sha256)
            {
                error = file.name + ": declared sha256 " + expectedHash + " but the file is " + file.sha256;
                return false;
            }
            found = cache.emplace(path, std::make_shared<const std::vector<float>>(std::move(file.samples))).first;
        }
        zone.samples = found->second;
        zone.name = juce::File(SampleFile::resolve(path)).getFileName().toStdString();

        const double frames = static_cast<double>(zone.length());
        zone.start = std::clamp(startSeconds * rate, 0.0, frames - 2.0);
        zone.end   = endSeconds < 0.0 ? frames : std::clamp(endSeconds * rate, zone.start + 2.0, frames);
        if (loopStartSeconds >= 0.0 && loopEndSeconds > loopStartSeconds)
        {
            zone.loopStart = std::clamp(loopStartSeconds * rate, zone.start, zone.end - 2.0);
            zone.loopEnd   = std::clamp(loopEndSeconds * rate, zone.loopStart + 2.0, zone.end);
            zone.fade      = std::clamp(fadeSeconds * rate, 0.0, zone.loopEnd - zone.loopStart);
        }
        return true;
    }
};

}  // namespace valis::dsp

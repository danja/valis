// src/ops/SongCheck.cpp
#include "valis/SongCheck.h"

#include "valis/AudioFile.h"
#include "valis/MidiScore.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace valis {

namespace {

std::string under(const std::string& directory, const std::string& path)
{
    return (! path.empty() && path.front() == '/') ? path : directory + "/" + path;
}

double toDb(double meanSquare)
{
    return meanSquare > 1.0e-12 ? 10.0 * std::log10(meanSquare) : -120.0;
}

std::string barList(const std::vector<int>& bars)
{
    std::string out;
    for (std::size_t i = 0; i < bars.size() && i < 12; ++i)
        out += (i ? ", " : "") + std::to_string(bars[i]);
    if (bars.size() > 12)
        out += ", ... (" + std::to_string(bars.size()) + " in all)";
    return out;
}

} // namespace

SongCheckResult checkSongStems(const Song& song, const std::string& songDirectory, const std::string& wavDirectory,
                               double silenceDb)
{
    SongCheckResult result;
    result.ok = true;
    const int totalBars = song.totalBars();

    for (const auto& s : song.stems)
    {
        StemReport report;
        report.name = s.name;
        report.isReturn = s.isReturn;

        StereoAudio audio;
        std::string error;
        if (! readAudioFile(wavDirectory + "/" + s.name + ".wav", audio, error))
        {
            report.problems.push_back("no wav to check (" + error + ")");
            result.ok = false;
            result.stems.push_back(std::move(report));
            continue;
        }
        report.found = true;

        // Level over the whole stem, and anything that is not a sample.
        double sumSquares = 0.0;
        for (std::size_t i = 0; i < audio.frames(); ++i)
            for (const float v : {audio.left[i], audio.right[i]})
            {
                if (! std::isfinite(v))
                {
                    report.nonFinite = true;
                    continue;
                }
                report.peak = std::max(report.peak, static_cast<double>(std::fabs(v)));
                sumSquares += static_cast<double>(v) * v;
            }
        report.rmsDb = toDb(sumSquares / std::max<std::size_t>(1, audio.frames() * 2));
        report.clipped = report.peak > 1.0;
        if (report.nonFinite)
            report.problems.push_back("holds samples that are not finite");
        if (report.clipped)
            report.problems.push_back("peaks above full scale");
        if (report.rmsDb < silenceDb)
            report.problems.push_back("is silent over its whole length");

        // The notes, by bar.
        std::vector<TimedNote> notes;
        if (! s.midi.empty())
        {
            const auto midiPath = under(songDirectory, s.midi);
            if (readMidiScore(midiPath, song.tempo, audio.sampleRate, notes, error))
                report.hasMidi = true;
            else
                report.problems.push_back("MIDI not read (" + error + ")");
        }

        const double barSamples = song.barSeconds() * audio.sampleRate;
        std::vector<int> noteCount(static_cast<std::size_t>(totalBars), 0);
        for (const auto& n : notes)
            if (n.on && n.velocity > 0.0f)
            {
                ++report.notes;
                const auto bar = static_cast<long long>(std::floor(static_cast<double>(n.sample) / barSamples));
                if (bar >= 0 && bar < totalBars)
                    ++noteCount[static_cast<std::size_t>(bar)];
            }

        for (int bar = 0; bar < totalBars; ++bar)
        {
            const auto from = static_cast<std::size_t>(std::llround(bar * barSamples));
            const auto to = std::min(audio.frames(), static_cast<std::size_t>(std::llround((bar + 1) * barSamples)));
            double sum = 0.0;
            for (std::size_t i = from; i < to; ++i)
                if (std::isfinite(audio.left[i]) && std::isfinite(audio.right[i]))
                    sum += static_cast<double>(audio.left[i]) * audio.left[i] + static_cast<double>(audio.right[i]) * audio.right[i];

            BarCheck b;
            b.bar = bar + 1;
            if (const auto place = song.locate(bar))
                b.section = song.sections[static_cast<std::size_t>(place->section)].name;
            b.notes = noteCount[static_cast<std::size_t>(bar)];
            b.rmsDb = to > from ? toDb(sum / static_cast<double>((to - from) * 2)) : -120.0;
            if (b.notes > 0 && b.rmsDb < silenceDb)
                report.silentBars.push_back(b.bar);
            report.bars.push_back(std::move(b));
        }

        if (report.hasMidi && report.notes == 0)
            report.problems.push_back("its MIDI file holds no notes");
        if (! report.silentBars.empty())
            report.problems.push_back("is silent in bars that have notes: " + barList(report.silentBars));
        if (! report.problems.empty())
            result.ok = false;
        result.stems.push_back(std::move(report));
    }
    return result;
}

} // namespace valis

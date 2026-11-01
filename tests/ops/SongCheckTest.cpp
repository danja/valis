// tests/ops/SongCheckTest.cpp
//
// Checking rendered stems against the score: a bar that has notes and no sound is found by its number, a stem that
// plays where the score says is clean, and a missing wav or an empty MIDI file is named.

#include "valis/AudioFile.h"
#include "valis/Song.h"
#include "valis/SongCheck.h"
#include "valis/TurtleStore.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

void writeMidi(const juce::File& file, const std::vector<int>& bars)
{
    constexpr int ppq = 480;
    juce::MidiMessageSequence sequence;
    for (const int bar : bars)                      // 4/4: a bar is four beats
    {
        sequence.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), bar * 4.0 * ppq);
        sequence.addEvent(juce::MidiMessage::noteOff(1, 60), (bar * 4.0 + 1.0) * ppq);
    }
    sequence.updateMatchedPairs();
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(sequence);
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream out(file);
    assert(out.openedOk());
    midi.writeTo(out);
}

StereoAudio tone(double seconds, double soundUntil)
{
    StereoAudio audio;
    audio.sampleRate = kRate;
    const auto n = static_cast<std::size_t>(seconds * kRate);
    audio.left.assign(n, 0.0f);
    for (std::size_t i = 0; i < n && static_cast<double>(i) / kRate < soundUntil; ++i)
        audio.left[i] = 0.3f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(i) / kRate));
    audio.right = audio.left;
    return audio;
}

const StemReport* find(const SongCheckResult& r, const std::string& name)
{
    for (const auto& s : r.stems)
        if (s.name == name) return &s;
    return nullptr;
}

bool mentions(const StemReport& s, const std::string& text)
{
    for (const auto& p : s.problems)
        if (p.find(text) != std::string::npos) return true;
    return false;
}

} // namespace

int main()
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("valis-song-check-test");
    directory.deleteRecursively();
    directory.createDirectory();
    const auto root = directory.getFullPathName().toStdString();

    // 120 bpm in 4/4: a bar is 2 s. Four bars.
    const char* text = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix : <urn:test:check#> .
:s a val:Song ; val:tempo 120 ; val:hasSection :a ; val:hasPart :good , :cut , :nowav , :empty .
:a a val:Section ; val:index 1 ; val:bars 4 .
:good  a val:Part ; val:index 1 ; val:circuit "x.ttl" .
:cut   a val:Part ; val:index 2 ; val:circuit "x.ttl" .
:nowav a val:Part ; val:index 3 ; val:circuit "x.ttl" .
:empty a val:Part ; val:index 4 ; val:circuit "x.ttl" .
)";
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> errors;
    const bool parsed = store.parse(text, "urn:test:check", errors);
    assert(parsed);
    Song song;
    std::vector<Diagnostic> diagnostics;
    const bool read = Song::read(store, song, diagnostics);
    assert(read);

    writeMidi(directory.getChildFile("midi/good.mid"), {0, 1, 2, 3});
    writeMidi(directory.getChildFile("midi/cut.mid"), {0, 1, 2, 3});
    writeMidi(directory.getChildFile("midi/nowav.mid"), {0});
    writeMidi(directory.getChildFile("midi/empty.mid"), {});

    std::string error;
    bool written = writeWavFile(root + "/wavs/good.wav", tone(8.0, 8.0), error);
    assert(written);
    written = writeWavFile(root + "/wavs/cut.wav", tone(8.0, 4.0), error);      // sounds in bars 1 and 2 only
    assert(written);
    written = writeWavFile(root + "/wavs/empty.wav", tone(8.0, 8.0), error);
    assert(written);

    const auto result = checkSongStems(song, root, root + "/wavs");
    assert(! result.ok && result.stems.size() == 4);

    const auto* good = find(result, "good");
    assert(good != nullptr && good->problems.empty() && good->notes == 4 && good->bars.size() == 4);
    assert(good->rmsDb > -20.0 && good->rmsDb < -10.0);                         // 0.3 sine: -13.4 dB
    assert(good->bars[2].notes == 1 && good->bars[2].section == "a");

    const auto* cut = find(result, "cut");
    assert(cut != nullptr && cut->silentBars == std::vector<int>({3, 4}));
    assert(mentions(*cut, "bars that have notes: 3, 4"));
    assert(cut->bars[0].rmsDb > -20.0 && cut->bars[3].rmsDb < -100.0);

    const auto* nowav = find(result, "nowav");
    assert(nowav != nullptr && ! nowav->found && mentions(*nowav, "no wav"));

    const auto* empty = find(result, "empty");
    assert(empty != nullptr && mentions(*empty, "holds no notes"));

    // Notes are placed by stream position: a note-on at the very start of the last bar belongs to that bar.
    assert(good->bars[3].notes == 1 && good->bars[0].notes == 1);

    directory.deleteRecursively();
    std::puts("SongCheckTest passed");
    return 0;
}

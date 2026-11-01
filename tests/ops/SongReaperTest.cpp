// tests/ops/SongReaperTest.cpp
//
// The Reaper project written from a song: a track for each stem that has a wav, volumes that keep the mix's balance with
// the loudest at 0 dB, the pan and the tempo from the song, and no replacing a project that exists.

#include "valis/AudioFile.h"
#include "valis/Song.h"
#include "valis/SongReaper.h"
#include "valis/TurtleStore.h"

#include <juce_core/juce_core.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

StereoAudio sine(double amplitude)
{
    StereoAudio audio;
    audio.sampleRate = kRate;
    audio.left.resize(static_cast<std::size_t>(4.0 * kRate));
    for (std::size_t i = 0; i < audio.left.size(); ++i)
        audio.left[i] = static_cast<float>(amplitude * std::sin(2.0 * 3.14159265358979 * 300.0 * static_cast<double>(i) / kRate));
    audio.right = audio.left;
    return audio;
}

} // namespace

int main()
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("valis-song-reaper-test");
    directory.deleteRecursively();
    directory.createDirectory();
    const auto root = directory.getFullPathName().toStdString();

    const char* text = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix : <urn:test:reaper#> .
:s a val:Song ; val:title "Reaper Test" ; val:tempo 90 ; val:beatsPerBar 3 ; val:hasSection :a ;
    val:hasPart :loud , :quiet , :gone , :dry .
:a a val:Section ; val:index 1 ; val:bars 2 .
:loud  a val:Part ; val:index 1 ; val:circuit "x.ttl" ; val:level -20 ; val:pan -0.5 .
:quiet a val:Part ; val:index 2 ; val:circuit "x.ttl" ; val:level -26 ; val:pan 0.25 .
:gone  a val:Part ; val:index 3 ; val:circuit "x.ttl" .
:dry   a val:Part ; val:index 4 ; val:circuit "x.ttl" ; val:level -30 ; val:silent true .
)";
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> errors;
    const bool parsed = store.parse(text, "urn:test:reaper", errors);
    assert(parsed);
    Song song;
    std::vector<Diagnostic> diagnostics;
    const bool read = Song::read(store, song, diagnostics);
    assert(read);

    // The same signal in each, so the gains differ by exactly the difference of the levels.
    std::string error;
    for (const char* name : {"loud", "quiet", "dry"})
    {
        const bool written = writeWavFile(root + "/wavs/" + name + ".wav", sine(0.2), error);
        assert(written);
    }

    const auto project = root + "/test.rpp";
    const auto result = writeReaperProject(song, root + "/wavs", project, false);
    if (! result.ok)
        std::printf("  %s\n", result.error.c_str());
    assert(result.ok && result.tracks.size() == 3);
    assert(result.missing.size() == 1 && result.missing[0] == "gone");

    // loud is at -20, quiet at -26, dry at -30, from one signal: 0, -6 and -10 dB below the loudest
    assert(result.tracks[0].name == "loud" && std::abs(result.tracks[0].volumeDb) < 1.0e-9);
    assert(std::abs(result.tracks[1].volumeDb + 6.0) < 1.0e-6 && std::abs(result.tracks[2].volumeDb + 10.0) < 1.0e-6);
    assert(result.tracks[2].muted && ! result.tracks[0].muted);
    assert(std::abs(result.tracks[0].seconds - 4.0) < 1.0e-6);

    const auto body = juce::File(juce::String(project)).loadFileAsString().toStdString();
    assert(body.find("TEMPO 90 3 4") != std::string::npos);
    assert(body.find("SAMPLERATE 48000") != std::string::npos);
    assert(body.find("NAME \"quiet\"") != std::string::npos && body.find("VOLPAN 0.501187 0.25") != std::string::npos);
    assert(body.find("VOLPAN 1 -0.5") != std::string::npos);
    assert(body.find("MUTESOLO 1 0 0") != std::string::npos);
    assert(body.find("FILE \"" + root + "/wavs/loud.wav\"") != std::string::npos);
    assert(body.find("gone") == std::string::npos);

    // A project that exists is not replaced, unless asked.
    const auto again = writeReaperProject(song, root + "/wavs", project, false);
    assert(! again.ok && again.error.find("not replaced") != std::string::npos);
    assert(writeReaperProject(song, root + "/wavs", project, true).ok);

    // MIDI mode: a track for each part that has a MIDI file, holding that file; volumes still come from the stems
    for (const char* name : {"loud", "quiet"})
        directory.getChildFile("midi").getChildFile(juce::String(name) + ".mid").create();      // only its existence matters here
    const auto midiProject = root + "/midi-test.rpp";
    const auto midi = writeReaperProject(song, root + "/wavs", midiProject, false, true, root);
    if (! midi.ok)
        std::printf("  %s\n", midi.error.c_str());
    assert(midi.ok && midi.tracks.size() == 2);                              // dry and gone have no MIDI file
    assert(midi.missing.size() == 2);
    assert(midi.tracks[0].midiPath == root + "/midi/loud.mid" && midi.tracks[0].circuit == "x.ttl");
    assert(std::abs(midi.tracks[1].volumeDb + 6.0) < 1.0e-6);
    const auto midiBody = juce::File(juce::String(midiProject)).loadFileAsString().toStdString();
    assert(midiBody.find("<SOURCE MIDI") != std::string::npos && midiBody.find("WAVE") == std::string::npos);
    assert(midiBody.find("FILE \"" + root + "/midi/quiet.mid\"") != std::string::npos);
    assert(midiBody.find("LENGTH 14") != std::string::npos);                  // 4 s of bars and the 10 s tail

    // No stems at all.
    const auto none = writeReaperProject(song, root + "/nothing", root + "/none.rpp", false);
    assert(! none.ok && none.error.find("render the song first") != std::string::npos);

    directory.deleteRecursively();
    std::puts("SongReaperTest passed");
    return 0;
}

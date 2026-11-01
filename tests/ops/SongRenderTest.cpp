// tests/ops/SongRenderTest.cpp
//
// Rendering and mixing a song through the operations, as the MCP tools and valis-song call them: a song file, a MIDI
// file per part and the example circuits go in, and a wav per stem comes out. The notes are placed by stream position,
// so a train of notes that do not fall on block boundaries must come out as exactly that many notes.

#include "valis/AudioFile.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/Ops.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

struct Note { double beat, beats; int key; };

void writeMidi(const juce::File& file, const std::vector<Note>& notes)
{
    constexpr int ppq = 480;
    juce::MidiMessageSequence sequence;
    for (const auto& n : notes)
    {
        sequence.addEvent(juce::MidiMessage::noteOn(1, n.key, static_cast<juce::uint8>(100)), n.beat * ppq);
        sequence.addEvent(juce::MidiMessage::noteOff(1, n.key), (n.beat + n.beats) * ppq);
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

void writeText(const juce::File& file, const std::string& text)
{
    file.getParentDirectory().createDirectory();
    std::ofstream(file.getFullPathName().toStdString()) << text;
}

struct Rig
{
    Ontology ontology;
    ElementRegistry registry = makeDefaultRegistry();
    juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("valis-songrender-test");

    Rig()
    {
        std::vector<std::string> errors;
        ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool loaded = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(loaded);
        directory.deleteRecursively();
        directory.createDirectory();
    }
    ~Rig() { directory.deleteRecursively(); }

    OpDispatcher ops()
    {
        OpContext context;
        context.ontology = &ontology;
        context.registry = &registry;
        return OpDispatcher(context);
    }

    std::string song() const { return directory.getChildFile("song.ttl").getFullPathName().toStdString(); }

    juce::File wav(const std::string& stem) const { return directory.getChildFile("wavs").getChildFile(juce::String(stem) + ".wav"); }
};

std::string songText(const std::string& leadMidi = "midi/lead.mid")
{
    return R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix : <urn:test:songrender#> .
:s a val:Song ; val:title "Render Test" ; val:tempo 120 ; val:tailSeconds 2 ; val:roomAmount 0 ; val:loudness -20 ;
    val:hasSection :a , :b ; val:hasPart :lead , :pad ; val:hasReturn :lead-verb .
:a a val:Section ; val:index 1 ; val:bars 2 .
:b a val:Section ; val:index 2 ; val:bars 1 ; val:level -3 .
:lead a val:Part ; val:index 1 ; val:circuit ")" + std::string(VALIS_EXAMPLES_DIR) + R"(/subsynth.ttl" ; val:midi ")" + leadMidi + R"(" ; val:level -18 ; val:pan -0.3 ; val:highpass 80 .
:pad  a val:Part ; val:index 2 ; val:circuit ")" + std::string(VALIS_EXAMPLES_DIR) + R"(/subsynth.ttl" ; val:midi "midi/pad.mid" ; val:level -24 ; val:pan 0.3 .
:lead-verb a val:Return ; val:source :lead ; val:circuit ")" + std::string(VALIS_EXAMPLES_DIR) + R"(/reverb-return.ttl" ; val:level -30 .
)";
}

/// Three short notes two seconds apart, each starting between block boundaries (512 samples is 0.0107 s).
void writeScore(const Rig& rig)
{
    writeMidi(rig.directory.getChildFile("midi/lead.mid"), {{0.6, 0.5, 60}, {4.6, 0.5, 64}, {8.6, 0.5, 67}});
    writeMidi(rig.directory.getChildFile("midi/pad.mid"), {{0.0, 4.0, 48}});
}

bool read(const juce::File& file, StereoAudio& audio)
{
    std::string error;
    const bool ok = readAudioFile(file.getFullPathName().toStdString(), audio, error);
    if (! ok)
        std::printf("  %s\n", error.c_str());
    return ok;
}

/// The number of separate sounds: stretches where the 10 ms RMS is within 20 dB of the loudest, separated by at
/// least 0.2 s below that.
int countNotes(const StereoAudio& audio)
{
    const std::size_t w = 480;
    const auto windowRms = [&](std::size_t at)
    {
        double e = 0.0;
        for (std::size_t i = at; i < at + w; ++i)
            e += 0.5 * (static_cast<double>(audio.left[i]) * audio.left[i] + static_cast<double>(audio.right[i]) * audio.right[i]);
        return std::sqrt(e / static_cast<double>(w));
    };
    double loudest = 0.0;
    for (std::size_t at = 0; at + w <= audio.frames(); at += w)
        loudest = std::max(loudest, windowRms(at));

    int notes = 0;
    bool sounding = false;
    std::size_t quiet = 0;
    for (std::size_t at = 0; at + w <= audio.frames(); at += w)
    {
        const bool loud = windowRms(at) > 0.1 * loudest;
        if (loud)
        {
            if (! sounding) ++notes;
            sounding = true;
            quiet = 0;
        }
        else if (sounding && ++quiet * w > static_cast<std::size_t>(0.2 * kRate))
            sounding = false;
    }
    return notes;
}

double rms(const StereoAudio& a, double from, double to)
{
    const auto i0 = static_cast<std::size_t>(from * kRate), i1 = std::min(a.frames(), static_cast<std::size_t>(to * kRate));
    double sum = 0.0;
    for (std::size_t i = i0; i < i1; ++i)
        sum += static_cast<double>(a.left[i]) * a.left[i] + static_cast<double>(a.right[i]) * a.right[i];
    return std::sqrt(sum / 2.0 / static_cast<double>(i1 - i0));
}

void testEveryStemIsRenderedWhereTheScoreSaysAndNowhereElse()
{
    Rig rig;
    writeScore(rig);
    writeText(rig.directory.getChildFile("song.ttl"), songText());

    const auto result = rig.ops().renderSong(rig.song(), {}, 3);
    if (! result.ok)
        for (const auto& d : result.diagnostics) std::printf("  %s\n", d.message.c_str());
    assert(result.ok);
    assert(result.value.find("\"lead-verb\"") != std::string::npos);

    StereoAudio lead, pad, verb;
    assert(read(rig.wav("lead"), lead) && read(rig.wav("pad"), pad) && read(rig.wav("lead-verb"), verb));

    // 3 bars of 2 s and a 2 s tail; the return is as long as the stem it is fed
    assert(lead.frames() == static_cast<std::size_t>(8 * kRate) && pad.frames() == lead.frames());
    assert(verb.frames() == lead.frames() && lead.sampleRate == kRate);

    // Silent until the first note, which is at 0.3 s, and then sounding; the onset is within a few
    // milliseconds of it and not at the next block boundary (0.3 s is sample 14400, 28.1 blocks in).
    assert(rms(lead, 0.0, 0.29) == 0.0);
    std::size_t onset = 0;
    while (onset < lead.frames() && std::abs(lead.left[onset]) < 1.0e-5f && std::abs(lead.right[onset]) < 1.0e-5f) ++onset;
    if (onset < 14400 || onset > 14400 + 1200)
        std::printf("  first sound at sample %zu, the note is at 14400\n", onset);
    assert(onset >= 14400 && onset <= 14400 + 1200);
    assert(rms(lead, 0.35, 0.6) > 0.01);

    // Three notes were played, so three come out.
    const int count = countNotes(lead);
    if (count != 3)
        std::printf("  counted %d notes in the lead, wanted 3\n", count);
    assert(count == 3);

    // The pad's one long note holds across the first 4 beats (2 s) and has ended by 4 s.
    assert(rms(pad, 0.3, 1.8) > 0.01);
    assert(rms(pad, 3.5, 4.0) < 0.001);

    // The return is the lead through the reverb circuit: not silent, not the same
    assert(rms(verb, 0.0, 8.0) > 1.0e-4 && lead.left != verb.left);
}

void testRenderingTwiceGivesTheSameSamples()
{
    Rig rig;
    writeScore(rig);
    writeText(rig.directory.getChildFile("song.ttl"), songText());
    assert(rig.ops().renderSong(rig.song(), {}, 3).ok);
    StereoAudio first, again;
    assert(read(rig.wav("lead"), first));
    assert(rig.ops().renderSong(rig.song(), {}, 1).ok);       // and on one thread rather than three
    assert(read(rig.wav("lead"), again));
    assert(first.left == again.left && first.right == again.right);
}

void testOnlyRedoesThePartsNamedAndTheirReturns()
{
    Rig rig;
    writeScore(rig);
    writeText(rig.directory.getChildFile("song.ttl"), songText());

    assert(rig.ops().renderSong(rig.song(), {"pad"}, 3).ok);
    assert(rig.wav("pad").existsAsFile() && ! rig.wav("lead").existsAsFile() && ! rig.wav("lead-verb").existsAsFile());

    assert(rig.ops().renderSong(rig.song(), {"lead"}, 3).ok);
    assert(rig.wav("lead").existsAsFile() && rig.wav("lead-verb").existsAsFile());

    const auto unknown = rig.ops().renderSong(rig.song(), {"nothing"}, 3);
    assert(! unknown.ok && unknown.diagnostics.front().message.find("nothing") != std::string::npos);
    const auto asReturn = rig.ops().renderSong(rig.song(), {"lead-verb"}, 3);
    assert(! asReturn.ok);
}

void testAStemThatCannotBeRenderedIsNamedAndTheOthersStillAre()
{
    Rig rig;
    writeScore(rig);
    writeText(rig.directory.getChildFile("song.ttl"), songText("midi/missing.mid"));

    const auto result = rig.ops().renderSong(rig.song(), {}, 3);
    assert(! result.ok);
    const auto message = result.diagnostics.front().message;
    if (message.find("missing.mid") == std::string::npos)
        std::printf("  the failure should name the file: %s\n", message.c_str());
    assert(message.find("lead") != std::string::npos && message.find("missing.mid") != std::string::npos);
    assert(rig.wav("pad").existsAsFile());              // the others went ahead
    assert(! rig.wav("lead").existsAsFile());

    // A circuit that is not there is also a located failure, not a crash.
    writeText(rig.directory.getChildFile("bad.ttl"), "this is not turtle");
    auto text = songText();
    const auto at = text.find(std::string(VALIS_EXAMPLES_DIR) + "/subsynth.ttl");
    text.replace(at, std::string(VALIS_EXAMPLES_DIR).size() + 13, rig.directory.getChildFile("bad.ttl").getFullPathName().toStdString());
    writeText(rig.directory.getChildFile("song.ttl"), text);
    const auto broken = rig.ops().renderSong(rig.song(), {}, 3);
    assert(! broken.ok && broken.diagnostics.front().message.find("bad.ttl") != std::string::npos);

    assert(! rig.ops().renderSong(rig.directory.getChildFile("nothing.ttl").getFullPathName().toStdString(), {}, 3).ok);
}

void testTheMixIsWrittenOnceAndEveryVersionKept()
{
    Rig rig;
    writeScore(rig);
    writeText(rig.directory.getChildFile("song.ttl"), songText());

    // nothing rendered yet
    assert(! rig.ops().mixSong(rig.song(), "v1", false, std::nullopt).ok);

    assert(rig.ops().renderSong(rig.song(), {}, 3).ok);
    const auto mixed = rig.ops().mixSong(rig.song(), "v1", true, std::nullopt);
    if (! mixed.ok)
        for (const auto& d : mixed.diagnostics) std::printf("  %s\n", d.message.c_str());
    assert(mixed.ok);
    assert(mixed.value.find("render-test-v1.wav") != std::string::npos);
    assert(mixed.value.find("\"sections\"") != std::string::npos && mixed.value.find("\"targetLufs\"") != std::string::npos);

    const auto file = rig.directory.getChildFile("mixes/render-test-v1.wav");
    assert(file.existsAsFile());
    StereoAudio mix;
    assert(read(file, mix));
    assert(mix.frames() == static_cast<std::size_t>(10 * kRate));          // 6 s of bars and 4 s after them

    // asking again does not replace it, unless told to
    const auto again = rig.ops().mixSong(rig.song(), "v1", true, std::nullopt);
    assert(! again.ok && again.diagnostics.front().message.find("already exists") != std::string::npos);
    assert(rig.ops().mixSong(rig.song(), "v1", true, std::nullopt, true).ok);
    assert(rig.ops().mixSong(rig.song(), "v2", true, -18.0).ok && rig.directory.getChildFile("mixes/render-test-v2.wav").existsAsFile());

    // a version cannot name a place outside the mixes directory
    assert(rig.ops().mixSong(rig.song(), "../../escape", false, std::nullopt).ok);
    assert(! rig.directory.getParentDirectory().getChildFile("escape-render-test.wav").existsAsFile());
    assert(! rig.ops().mixSong(rig.song(), "///", false, std::nullopt).ok);
}

} // namespace

int main()
{
    testEveryStemIsRenderedWhereTheScoreSaysAndNowhereElse();
    testRenderingTwiceGivesTheSameSamples();
    testOnlyRedoesThePartsNamedAndTheirReturns();
    testAStemThatCannotBeRenderedIsNamedAndTheOthersStillAre();
    testTheMixIsWrittenOnceAndEveryVersionKept();
    std::printf("SongRenderTest: passed\n");
    return 0;
}

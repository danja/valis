// tests/ops/ScoreWriteTest.cpp
//
// A score played into notes and MIDI files. Notes are placed by stream position, so the tests count exactly how many fall
// in each bar and where, read the files back, and check that the same seed always gives the same notes.

#include "valis/Score.h"
#include "valis/ScoreWrite.h"
#include "valis/Song.h"
#include "valis/TurtleStore.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using namespace valis;

namespace {

const char* kSongText = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix : <urn:test:song#> .
:s a val:Song ; val:tempo 120 ; val:hasSection :intro , :verse ; val:hasPart :kit , :bass , :stabs , :free .
:intro a val:Section ; val:index 1 ; val:bars 4 .
:verse a val:Section ; val:index 2 ; val:bars 8 .
:kit   a val:Part ; val:index 1 ; val:circuit "k.ttl" .
:bass  a val:Part ; val:index 2 ; val:circuit "b.ttl" ; val:midi "notes/low.mid" .
:stabs a val:Part ; val:index 3 ; val:circuit "s.ttl" .
:free  a val:Part ; val:index 4 ; val:circuit "f.ttl" .
)";

std::string scoreText(const std::string& feel, int seed)
{
    return R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix : <urn:test:score#> .
:score a val:Score ; val:seed )" + std::to_string(seed) + R"( ; val:progression "C F" ; val:barsPerChord 2 ;
    val:hasChord :C , :F ; val:hasPattern :beat , :walk , :stab ; val:hasFeel :f ;
    val:hasPlay :p1 , :p2 , :p3 .
:C a val:Chord ; val:root 36 ; val:pickup 37 ; val:tones "60 64 67" .
:F a val:Chord ; val:root 41 ; val:tones "60 65 69" .
:beat a val:Pattern ; val:hasLane
    [ val:key 36 ; val:steps "X.......x......." ] ,
    [ val:key 42 ; val:steps ".x.x.x.x.x.x.x.x" ; val:velocity 70 ] .
:walk a val:Pattern ; val:bars 2 ; val:hasLane
    [ val:voice "root" ; val:steps "x...............x..............." ; val:length 4 ] ,
    [ val:voice "pickup" ; val:steps "..............x................." ; val:length 1 ] .
:stab a val:Pattern ; val:hasLane
    [ val:voice "chord" ; val:steps "..x............." ; val:length 2 ; val:octave 1 ] .
)" + feel + R"(
:p1 a val:Play ; val:section "intro" ; val:part "kit" ; val:pattern :beat .
:p2 a val:Play ; val:section "verse" ; val:part "bass" ; val:pattern :walk ; val:fromBar 3 ; val:every 2 .
:p3 a val:Play ; val:section "verse" ; val:part "stabs" ; val:pattern :stab ; val:every 2 ; val:toBar 6 .
)";
}

Song makeSong()
{
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> errors;
    const bool parsed = store.parse(kSongText, "urn:test:song", errors);
    assert(parsed);
    Song song;
    std::vector<Diagnostic> diagnostics;
    const bool read = Song::read(store, song, diagnostics);
    assert(read);
    return song;
}

Score makeScore(const Song& song, const std::string& feel, int seed)
{
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> errors;
    const bool parsed = store.parse(scoreText(feel, seed), "urn:test:score", errors);
    assert(parsed);
    Score score;
    std::vector<Diagnostic> diagnostics;
    const bool read = Score::read(store, song, score, diagnostics);
    if (! read)
        for (const auto& d : diagnostics) std::fprintf(stderr, "  %s\n", d.message.c_str());
    assert(read);
    return score;
}

int countIn(const std::vector<ScoreNote>& notes, double fromBeat, double toBeat, int key = -1)
{
    int n = 0;
    for (const auto& note : notes)
        if (note.beat >= fromBeat && note.beat < toBeat && (key < 0 || note.key == key)) ++n;
    return n;
}

} // namespace

int main()
{
    const auto song = makeSong();
    const auto exact = makeScore(song, ":f a val:Feel ; val:part \"free\" .", 1);   // a feel for a part the score does not play
    std::string error;

    // -- the kit: four bars of a one-bar pattern, exact. A bar is four beats, a step a quarter beat.
    std::vector<ScoreNote> kit;
    assert(expandScorePart(song, exact, "kit", kit, error));
    assert(kit.size() == 4 * (2 + 8));
    for (int bar = 0; bar < 4; ++bar)
    {
        assert(countIn(kit, bar * 4.0, bar * 4.0 + 4.0, 36) == 2);
        assert(countIn(kit, bar * 4.0, bar * 4.0 + 4.0, 42) == 8);
    }
    // the first kick is the accented one, on the first beat; the second is on step 8, beat 2
    assert(kit.front().beat == 0.0 && kit.front().key == 36 && kit.front().velocity == 110);
    bool secondKick = false;
    for (const auto& n : kit)
        if (n.key == 36 && n.beat == 2.0) { secondKick = true; assert(n.velocity == 90); }
    assert(secondKick);
    assert(std::abs(kit.front().beats - 0.2) < 1e-12);                  // 1 step of 0.25 beat, a gate of 0.8
    for (std::size_t i = 1; i < kit.size(); ++i)
        assert(kit[i - 1].beat < kit[i].beat || (kit[i - 1].beat == kit[i].beat && kit[i - 1].key <= kit[i].key));

    // -- the bass: the verse starts at bar 5 (4 bars of intro); the walk is two bars, from bar 3 of the verse, restarting every
    //    two bars, to the end: bars 3-4, 5-6, 7-8 of the verse. Each repeat has two roots, one a bar.
    std::vector<ScoreNote> bass;
    assert(expandScorePart(song, exact, "bass", bass, error));
    assert(bass.size() == 9);                                            // three repeats: two roots and a pickup each
    // verse bar 3 is song bar 7 (beat 24); the chord there is the second of "C F" at two bars each: bars 2-3 of the verse index
    // (bar-1)/2 = 1 -> F, root 41. Verse bar 4 -> index 1 -> F. Bars 5-6 -> index 2 -> C, 7-8 -> index 3 -> F.
    assert(bass[0].beat == 24.0 && bass[0].key == 41);
    assert(bass[1].beat == 27.5 && bass[1].key == 41);                  // F has no pickup, so the root
    assert(bass[2].beat == 28.0 && bass[2].key == 41);
    assert(bass[3].beat == 32.0 && bass[3].key == 36);
    assert(bass[4].beat == 35.5 && bass[4].key == 37);                  // C has one
    assert(bass[6].beat == 40.0 && bass[6].key == 41);
    assert(std::abs(bass[0].beats - 0.8) < 1e-12);                      // 4 steps of 0.25 beat, a gate of 0.8

    // -- the stabs: a one-bar pattern every second bar, bars 1 to 6 of the verse, three chord tones an octave up
    std::vector<ScoreNote> stabs;
    assert(expandScorePart(song, exact, "stabs", stabs, error));
    assert(stabs.size() == 3 * 3);                                       // bars 1, 3, 5
    assert(stabs[0].beat == 16.5 && stabs[0].key == 72 && stabs[1].key == 76 && stabs[2].key == 79);
    assert(countIn(stabs, 16.0, 20.0) == 3 && countIn(stabs, 20.0, 24.0) == 0 && countIn(stabs, 24.0, 28.0) == 3);

    // -- a part the score does not play
    std::vector<ScoreNote> none;
    assert(! expandScorePart(song, exact, "free", none, error) && error.find("does not play") != std::string::npos);

    // -- the feel: swing moves the second step of each pair; the hi-hat on steps 1, 3, 5... all move; the kicks do not
    const auto swung = makeScore(song, ":f a val:Feel ; val:part \"kit\" ; val:swing 0.6 .", 1);
    std::vector<ScoreNote> shuffled;
    assert(expandScorePart(song, swung, "kit", shuffled, error));
    assert(shuffled.size() == kit.size());
    assert(shuffled.front().beat == 0.0);
    for (const auto& n : shuffled)
        if (n.key == 42)
        {
            // step 1 is the second of pair 0: pair start 0, length 0.5 beat, so it falls at 0.6 * 0.5 = 0.3 beat
            const double within = std::fmod(n.beat, 0.5);
            assert(std::abs(within - 0.3) < 1e-12);
        }

    // -- human looseness: repeatable for a seed, different for another, never before the start
    const char* loose = ":f a val:Feel ; val:part \"kit\" ; val:timing 20 ; val:velocity 10 .";
    std::vector<ScoreNote> a, b, c;
    assert(expandScorePart(song, makeScore(song, loose, 85), "kit", a, error));
    assert(expandScorePart(song, makeScore(song, loose, 85), "kit", b, error));
    assert(expandScorePart(song, makeScore(song, loose, 86), "kit", c, error));
    assert(a.size() == kit.size() && a.size() == b.size());
    bool same = true, differs = false, moved = false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].beat != b[i].beat || a[i].velocity != b[i].velocity || a[i].key != b[i].key) same = false;
        if (a[i].beat != c[i].beat || a[i].velocity != c[i].velocity) differs = true;
        if (a[i].beat != kit[i].beat) moved = true;
        assert(a[i].beat >= 0.0 && a[i].velocity >= 1 && a[i].velocity <= 127);
    }
    assert(same && differs && moved);
    // a standard deviation of 20 ms is 0.04 beat at 120 bpm: nothing strays by more than a few of them
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, std::abs(a[i].beat - kit[i].beat));
    assert(worst < 7.0 * 0.04);

    // -- the files: the part's own val:midi, tempo, and exact note counts read back
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("valis-score-write-test");
    directory.deleteRecursively();
    directory.createDirectory();
    const auto root = directory.getFullPathName().toStdString();
    const auto written = writeScoreMidi(song, exact, root);
    if (! written.ok) std::printf("  %s\n", written.error.c_str());
    assert(written.ok && written.parts.size() == 3);

    const auto readBack = [&](const juce::File& file)
    {
        juce::FileInputStream in(file);
        assert(in.openedOk());
        juce::MidiFile midi;
        const bool readOk = midi.readFrom(in);
        assert(readOk && midi.getTimeFormat() == 480);
        const auto* track = midi.getTrack(0);
        int ons = 0;
        double lastOn = 0.0;
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto& m = track->getEventPointer(i)->message;
            if (m.isNoteOn()) { ++ons; lastOn = m.getTimeStamp(); }
        }
        return std::make_pair(ons, lastOn);
    };
    assert(readBack(directory.getChildFile("midi/kit.mid")).first == 40);
    const auto bassFile = readBack(directory.getChildFile("notes/low.mid"));
    assert(bassFile.first == 9 && bassFile.second == 44.0 * 480.0);     // the last root falls on beat 44
    assert(! directory.getChildFile("midi/free.mid").existsAsFile());   // not in the score, so not written

    // only some parts; an unknown one is refused
    directory.getChildFile("midi/kit.mid").deleteFile();
    const auto some = writeScoreMidi(song, exact, root, {"bass"});
    assert(some.ok && some.parts.size() == 1 && ! directory.getChildFile("midi/kit.mid").existsAsFile());
    assert(! writeScoreMidi(song, exact, root, {"free"}).ok);

    directory.deleteRecursively();
    std::puts("ScoreWriteTest passed");
    return 0;
}

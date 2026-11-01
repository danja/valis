// tests/model/ScoreTest.cpp
//
// A score file is read into patterns, chords, feels and a plan, and a file that does not fit its song fails with the
// reason, not with a half-built score.

#include "valis/Score.h"
#include "valis/Song.h"
#include "valis/TurtleStore.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace valis;

namespace {

const char* kSongText = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix : <urn:test:song#> .
:s a val:Song ; val:tempo 120 ; val:hasSection :intro , :verse ; val:hasPart :kit , :bass , :stabs .
:intro a val:Section ; val:index 1 ; val:bars 4 .
:verse a val:Section ; val:index 2 ; val:bars 8 .
:kit   a val:Part ; val:index 1 ; val:circuit "k.ttl" .
:bass  a val:Part ; val:index 2 ; val:circuit "b.ttl" .
:stabs a val:Part ; val:index 3 ; val:circuit "s.ttl" .
)";

const char* kScore = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix : <urn:test:score#> .
:score a val:Score ; val:seed 85 ; val:progression "C F" ; val:barsPerChord 2 ;
    val:hasChord :C , :F ; val:hasPattern :beat , :walk ; val:hasFeel :kitfeel ;
    val:hasPlay :p1 , :p2 .
:C a val:Chord ; val:root 36 ; val:tones "60 64 67" .
:F a val:Chord ; val:root 41 ; val:tones "60 65 69" .
:beat a val:Pattern ; val:hasLane
    [ val:key 36 ; val:steps "X... .... x... ...." ] ,
    [ val:key 38 ; val:steps "|....x.......x...|" ; val:velocity 100 ] .
:walk a val:Pattern ; val:bars 2 ; val:hasLane
    [ val:voice "root" ; val:steps "x...............x..............." ; val:length 4 ; val:octave 0 ] .
:kitfeel a val:Feel ; val:part "kit" ; val:timing 4 ; val:velocity 5 ; val:swing 0.54 .
:p1 a val:Play ; val:section "intro" ; val:part "kit" ; val:pattern :beat .
:p2 a val:Play ; val:section "verse" ; val:part "bass" ; val:pattern :walk ; val:fromBar 3 ; val:every 2 .
)";

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

bool read(const std::string& text, const Song& song, Score& score, std::vector<Diagnostic>& diagnostics)
{
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> errors;
    const bool parsed = store.parse(text, "urn:test:score", errors);
    assert(parsed);
    return Score::read(store, song, score, diagnostics);
}

std::string without(const std::string& what, const std::string& with)
{
    std::string text = kScore;
    const auto at = text.find(what);
    assert(at != std::string::npos);
    return text.replace(at, what.size(), with);
}

bool fails(const Song& song, const std::string& body, const std::string& reason)
{
    Score score;
    std::vector<Diagnostic> diagnostics;
    if (read(body, song, score, diagnostics))
        return false;
    for (const auto& d : diagnostics)
        if (d.message.find(reason) != std::string::npos)
            return true;
    std::printf("  wanted '%s', got:\n", reason.c_str());
    for (const auto& d : diagnostics)
        std::printf("    %s\n", d.message.c_str());
    return false;
}

} // namespace

int main()
{
    const auto song = makeSong();
    Score score;
    std::vector<Diagnostic> diagnostics;
    const bool ok = read(kScore, song, score, diagnostics);
    if (! ok)
        for (const auto& d : diagnostics) std::printf("  %s\n", d.message.c_str());
    assert(ok);

    assert(score.seed == 85 && score.barsPerChord == 2);
    assert(score.progression == std::vector<std::string>({"C", "F"}));
    assert(score.chords.at("F").root == 41 && score.chords.at("C").tones == std::vector<int>({60, 64, 67}));
    assert(score.patterns.size() == 2 && score.patterns.at("walk").bars == 2);

    const auto& beat = score.patterns.at("beat");
    assert(beat.lanes.size() == 2 && beat.stepsPerBeat == 4);
    bool sawKick = false, sawSnare = false;
    for (const auto& lane : beat.lanes)
    {
        if (lane.key == 36) { sawKick = true; assert(lane.steps == "X.......x......."); assert(lane.velocity == 90 && lane.accent == 20); }
        if (lane.key == 38) { sawSnare = true; assert(lane.steps == "....x.......x..." && lane.velocity == 100); }
    }
    assert(sawKick && sawSnare);                    // spaces and bars are dropped from the steps

    const auto* feel = score.feelOf("kit");
    assert(feel != nullptr && feel->timingMs == 4.0 && feel->velocity == 5.0 && feel->swing == 0.54);
    assert(score.feelOf("bass") == nullptr);

    assert(score.plays.size() == 2);
    for (const auto& p : score.plays)
        if (p.part == "bass")
            assert(p.section == "verse" && p.pattern == "walk" && p.fromBar == 3 && p.every == 2 && p.toBar == 0);

    // -- what is refused
    assert(fails(song, without("val:section \"intro\"", "val:section \"nowhere\""), "not a section of the song"));
    assert(fails(song, without("val:part \"kit\" ; val:pattern", "val:part \"horns\" ; val:pattern"), "not a part of the song"));
    assert(fails(song, without("val:pattern :beat .", "val:pattern :missing ."), "does not declare"));
    assert(fails(song, without("val:steps \"|....x.......x...|\"", "val:steps \"....x.......x\""), "need 16"));
    assert(fails(song, without("val:steps \"|....x.......x...|\"", "val:steps \"....z.......x...\""), "among its steps"));
    assert(fails(song, without("val:progression \"C F\"", "val:progression \"C G\""), "'G'"));
    assert(fails(song, without("val:progression \"C F\" ;", ""), "no val:progression"));
    assert(fails(song, without("val:voice \"root\"", "val:voice \"middle\""), "use chord, root, top or low"));
    assert(fails(song, without("val:voice \"root\"", "val:key 40 ; val:voice \"root\""), "both val:key and val:voice"));
    assert(fails(song, without("val:velocity 100", "val:velocity 0"), "runs from 1 to 127"));
    assert(fails(song, without("val:swing 0.54", "val:swing 0.9"), "val:swing"));
    assert(fails(song, without("val:fromBar 3", "val:fromBar 9"), "the section has 8"));
    assert(fails(song, without("val:tones \"60 64 67\"", "val:tones \"60 x 67\""), "MIDI keys"));
    assert(fails(song, without("val:part \"kit\" ; val:timing", "val:part \"nobody\" ; val:timing"), "not a part of the song"));
    assert(fails(song, "@prefix val: <http://purl.org/stuff/valis/> .\n@prefix : <urn:t#> .\n:s a val:Song .", "no val:Score"));

    std::puts("ScoreTest passed");
    return 0;
}

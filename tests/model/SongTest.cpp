// tests/model/SongTest.cpp
//
// A song file is read into sections, parts and returns, and a file that is not a usable song fails with the
// reason, not with a half-built song.

#include "valis/Song.h"
#include "valis/TurtleStore.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using namespace valis;

namespace {

const char* kPrefixes = R"(
@prefix val: <http://purl.org/stuff/valis/> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix : <urn:test:song#> .
)";

const char* kGood = R"(
:s a val:Song ; val:title "Test" ; val:tempo 120 ; val:loudness -14 ;
    val:hasSection :intro , :verse , :outro ;
    val:hasPart :bass , :kit ; val:hasReturn :kit-echo .
:outro a val:Section ; val:index 3 ; val:bars 4 ; val:level -3 .
:intro a val:Section ; val:index 1 ; val:bars 8 ; rdfs:label "Intro" ; val:level -4.5 .
:verse a val:Section ; val:index 2 ; val:bars 16 .
:kit  a val:Part ; val:index 2 ; val:circuit "circuits/kit.ttl" ; val:level -26 ; val:pan -0.25 ; val:highpass 40 .
:bass a val:Part ; val:index 1 ; val:circuit "circuits/bass.ttl" ; val:midi "midi/low.mid" ; val:level -20 .
:kit-echo a val:Return ; val:source :kit ; val:circuit "circuits/echo.ttl" ; val:level -30 ; val:pan 0.4 .
)";

bool read(const std::string& body, Song& song, std::vector<Diagnostic>& diagnostics)
{
    rdf::TurtleStore store;
    std::vector<rdf::ParseError> errors;
    const bool parsed = store.parse(std::string(kPrefixes) + body, "urn:test:song", errors);
    assert(parsed);
    return Song::read(store, song, diagnostics);
}

/// The good song with one line of it replaced, so each failure changes one thing.
std::string without(const std::string& what, const std::string& with)
{
    std::string text = kGood;
    const auto at = text.find(what);
    assert(at != std::string::npos);
    return text.replace(at, what.size(), with);
}

bool fails(const std::string& body, const std::string& reason)
{
    Song song;
    std::vector<Diagnostic> diagnostics;
    if (read(body, song, diagnostics))
        return false;
    for (const auto& d : diagnostics)
        if (d.message.find(reason) != std::string::npos)
            return true;
    std::printf("wanted '%s' in:\n", reason.c_str());
    for (const auto& d : diagnostics) std::printf("  %s\n", d.message.c_str());
    return false;
}

} // namespace

int main()
{
    Song song;
    std::vector<Diagnostic> diagnostics;
    const bool good = read(kGood, song, diagnostics);
    assert(good && diagnostics.empty());

    // The clock: 28 bars of 4 beats at 120 bpm is 56 seconds, and the tail goes on top.
    assert(song.title == "Test" && song.tempo == 120.0 && song.beatsPerBar == 4);
    assert(song.totalBars() == 28 && std::fabs(song.barSeconds() - 2.0) < 1e-9);
    assert(std::fabs(song.seconds() - 56.0) < 1e-9 && std::fabs(song.renderSeconds() - 66.0) < 1e-9);
    assert(song.loudness && *song.loudness == -14.0);

    // Sections come out in index order whatever order the file wrote them in, with where each starts.
    assert(song.sections.size() == 3);
    assert(song.sections[0].name == "intro" && song.sections[0].label == "Intro" && song.sections[0].level == -4.5);
    assert(song.sections[1].firstBar == 8 && song.sections[1].label == "verse" && song.sections[1].level == 0.0);
    assert(song.sections[2].firstBar == 24 && song.sections[2].bars == 4);
    assert(song.locate(0)->section == 0 && song.locate(7)->barInSection == 7);
    assert(song.locate(8)->section == 1 && song.locate(8)->barInSection == 0);
    assert(song.locate(27)->section == 2 && ! song.locate(28) && ! song.locate(-1));

    // Parts in index order, then returns; defaults fill in what the file leaves out.
    assert(song.stems.size() == 3);
    assert(song.stems[0].name == "bass" && song.stems[0].midi == "midi/low.mid" && song.stems[0].pan == 0.0);
    assert(song.stems[1].name == "kit" && song.stems[1].midi == "midi/kit.mid");
    assert(song.stems[1].pan == -0.25 && song.stems[1].highpass == 40.0 && song.stems[1].level == -26.0);
    assert(song.stems[2].name == "kit-echo" && song.stems[2].isReturn && song.stems[2].source == "kit");
    assert(song.stems[2].midi.empty());
    assert(song.findStem("bass") && ! song.findStem("nothing"));

    // The mix settings default, and a title is also a file name.
    assert(song.fadeSeconds == 4.0 && song.roomAmount == 0.10 && song.drive == 1.3);
    assert(song.slug() == "test");
    Song titled;
    assert(read(without("val:title \"Test\"", "val:title \"  Interstate Rain (v2)!  \""), titled, diagnostics));
    assert(titled.slug() == "interstate-rain-v2");
    Song blank;
    assert(read(without("val:title \"Test\"", "val:title \"???\""), blank, diagnostics) && blank.slug() == "song");
    Song set;
    assert(read(without("val:tempo 120", "val:tempo 120 ; val:fadeSeconds 3 ; val:roomAmount 0.12 ; val:drive 1.5"), set, diagnostics));
    assert(set.fadeSeconds == 3.0 && set.roomAmount == 0.12 && set.drive == 1.5);
    assert(fails(without("val:tempo 120", "val:tempo 120 ; val:fadeSeconds -1"), "val:fadeSeconds"));
    assert(fails(without("val:tempo 120", "val:tempo 120 ; val:roomAmount 2"), "val:roomAmount"));
    assert(fails(without("val:tempo 120", "val:tempo 120 ; val:drive 0"), "val:drive"));

    // The room's shape, a part that follows another's gain, and a return that is played.
    assert(song.roomDecay == 0.4 && song.roomLowpass == 4500.0);
    Song roomy;
    assert(read(without("val:tempo 120", "val:tempo 120 ; val:roomDecay 0.28 ; val:roomLowpass 5500"), roomy, diagnostics));
    assert(roomy.roomDecay == 0.28 && roomy.roomLowpass == 5500.0);
    assert(fails(without("val:tempo 120", "val:tempo 120 ; val:roomDecay 0"), "val:roomDecay"));
    assert(fails(without("val:tempo 120", "val:tempo 120 ; val:roomLowpass 50"), "val:roomLowpass"));

    Song following;
    assert(read(without("val:circuit \"circuits/kit.ttl\" ;", "val:circuit \"circuits/kit.ttl\" ; val:follows :bass ;"), following, diagnostics));
    assert(following.findStem("kit")->follows == "bass" && following.findStem("bass")->follows.empty());
    assert(fails(without("val:circuit \"circuits/kit.ttl\" ;", "val:circuit \"circuits/kit.ttl\" ; val:follows :kit ;"), "follows itself"));
    assert(fails(without("val:circuit \"circuits/kit.ttl\" ;", "val:circuit \"circuits/kit.ttl\" ; val:follows :nothing ;"), "not a part of this song"));
    assert(fails(without("val:circuit \"circuits/kit.ttl\" ;", "val:circuit \"circuits/kit.ttl\" ; val:follows :kit-echo ;"), "not a part of this song"));
    assert(fails(without("val:circuit \"circuits/kit.ttl\" ;", "val:circuit \"circuits/kit.ttl\" ; val:follows :bass ;") + ":bass val:follows :kit .", "itself follows"));
    assert(fails(without("val:source :kit ;", "val:source :kit ; val:follows :bass ;"), "cannot follow"));

    Song quiet;
    assert(read(without("val:circuit \"circuits/kit.ttl\" ;", "val:circuit \"circuits/kit.ttl\" ; val:silent true ;"), quiet, diagnostics));
    assert(quiet.findStem("kit")->silent && ! quiet.findStem("bass")->silent);
    assert(fails(without("val:source :kit ;", "val:source :kit ; val:silent true ;"), "cannot be val:silent"));
    assert(fails(without("val:circuit \"circuits/kit.ttl\" ;", "val:circuit \"circuits/kit.ttl\" ; val:silent maybe ;"), "true or false"));
    assert(fails(without("val:circuit \"circuits/kit.ttl\" ;", "val:circuit \"circuits/kit.ttl\" ; val:follows :bass ;") + ":bass val:silent true .", "not in the mix"));

    Song played;
    assert(read(without("val:source :kit ;", "val:source :kit ; val:midi \"midi/speed.mid\" ;"), played, diagnostics));
    assert(played.findStem("kit-echo")->midi == "midi/speed.mid" && song.findStem("kit-echo")->midi.empty());

    // A different meter changes the length of a bar.
    Song waltz;
    assert(read(without("val:tempo 120", "val:tempo 90 ; val:beatsPerBar 3"), waltz, diagnostics));
    assert(std::fabs(waltz.barSeconds() - 2.0) < 1e-9);

    // Each way a file can fail says why.
    assert(fails("", "no val:Song"));
    assert(fails(std::string(kGood) + ":t a val:Song .", "holds one val:Song"));
    assert(fails(without("val:tempo 120 ;", ""), "no val:tempo"));
    assert(fails(without("val:tempo 120", "val:tempo 900"), "between 20 and 400"));
    assert(fails(without("val:loudness -14", "val:loudness 3"), "LUFS"));
    assert(fails(without("val:index 2 ; val:bars 16", "val:index 4 ; val:bars 16"), "numbered 3 but should be 2"));
    assert(fails(without("val:index 2 ; val:bars 16", "val:index 1 ; val:bars 16"), "numbered 1 but should be 2"));
    assert(fails(without("val:bars 16", "val:bars 0"), "at least 1"));
    assert(fails(without("val:index 2 ; val:bars 16", "val:bars 16"), "no val:index"));
    assert(fails(without("val:index 2 ; val:bars 16", "val:index 2"), "no val:bars"));
    assert(fails(without("val:circuit \"circuits/kit.ttl\" ;", ""), "part kit has no val:circuit"));
    assert(fails(without("val:pan -0.25", "val:pan -1.5"), "pan of -1.5"));
    assert(fails(without("val:level -26", "val:level 6"), "level of 6"));
    assert(fails(without("val:highpass 40", "val:highpass -5"), "high-pass"));
    assert(fails(without("val:source :kit", "val:source :ghost"), "not a part of this song"));
    assert(fails(without("val:source :kit ;", ""), "no val:source"));
    assert(fails(without("val:source :kit", "val:source :kit-echo"), "not a part of this song"));

    // Two stems of one name, from IRIs that differ only in their namespace.
    assert(fails(std::string(kGood) + "<urn:other#bass> a val:Part ; val:circuit \"x.ttl\" .\n:s val:hasPart <urn:other#bass> .",
                 "two stems are called bass"));

    // A song with no parts or no sections is refused, not accepted empty.
    assert(fails(":s a val:Song ; val:tempo 100 ; val:hasSection :a .\n:a a val:Section ; val:index 1 ; val:bars 4 .",
                 "no parts"));
    assert(fails(":s a val:Song ; val:tempo 100 ; val:hasPart :p .\n:p a val:Part ; val:circuit \"c.ttl\" .",
                 "no sections"));

    std::puts("SongTest passed");
    return 0;
}

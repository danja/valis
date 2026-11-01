// src/model/Song.cpp
#include "valis/Song.h"

#include "valis/Vocabulary.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace valis {

namespace {

std::string localName(const std::string& iri)
{
    const auto at = iri.find_last_of("#/");
    return at == std::string::npos ? iri : iri.substr(at + 1);
}

bool isWhole(double v)
{
    return std::fabs(v - std::round(v)) < 1.0e-9;
}

struct Reader
{
    const rdf::TurtleStore& store;
    std::vector<Diagnostic>& diagnostics;
    bool ok = true;

    void fail(const std::string& message, const std::string& subject)
    {
        diagnostics.push_back({message, subject});
        ok = false;
    }

    std::optional<double> number(const rdf::Node& subject, const std::string& predicate, const std::string& what)
    {
        const auto value = store.object(subject, predicate);
        if (! value)
            return std::nullopt;
        if (const auto v = value.asDouble())
            return v;
        fail(what + " must be a number, not '" + std::string(value.string()) + "'", std::string(subject.string()));
        return std::nullopt;
    }

    std::string text(const rdf::Node& subject, const std::string& predicate)
    {
        const auto value = store.object(subject, predicate);
        return value ? std::string(value.string()) : std::string();
    }
};

} // namespace

int Song::totalBars() const
{
    int total = 0;
    for (const auto& s : sections) total += s.bars;
    return total;
}

std::optional<Song::Place> Song::locate(int bar) const
{
    if (bar < 0)
        return std::nullopt;
    for (std::size_t i = 0; i < sections.size(); ++i)
        if (bar < sections[i].firstBar + sections[i].bars)
            return Place{static_cast<int>(i), bar - sections[i].firstBar};
    return std::nullopt;
}

std::string Song::slug() const
{
    std::string out;
    bool dash = false;
    for (const char ch : title)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c))
        {
            if (dash && ! out.empty()) out += '-';
            out += static_cast<char>(std::tolower(c));
            dash = false;
        }
        else
            dash = true;
    }
    return out.empty() ? "song" : out;
}

const SongStem* Song::findStem(const std::string& name) const
{
    for (const auto& s : stems)
        if (s.name == name) return &s;
    return nullptr;
}

bool Song::read(const rdf::TurtleStore& store, Song& out, std::vector<Diagnostic>& diagnostics)
{
    Reader r{store, diagnostics};
    Song song;

    const auto songs = store.subjectsOfType(vocab::song::Song);
    if (songs.empty())
    {
        diagnostics.push_back({"there is no val:Song in this file", {}});
        return false;
    }
    if (songs.size() > 1)
    {
        diagnostics.push_back({"a file holds one val:Song; this one holds " + std::to_string(songs.size()), {}});
        return false;
    }
    const auto node = songs.front();
    song.id = std::string(node.string());
    song.title = r.text(node, vocab::song::title);
    if (song.title.empty()) song.title = r.text(node, vocab::rdfs::label);
    if (song.title.empty()) song.title = localName(song.id);

    // -- the clock
    if (const auto tempo = r.number(node, vocab::song::tempo, "val:tempo"))
    {
        song.tempo = *tempo;
        if (*tempo < 20.0 || *tempo > 400.0)
            r.fail("val:tempo is " + std::to_string(*tempo) + "; it must be between 20 and 400 beats per minute", song.id);
    }
    else
        r.fail("the song has no val:tempo", song.id);

    if (const auto beats = r.number(node, vocab::song::beatsPerBar, "val:beatsPerBar"))
    {
        song.beatsPerBar = static_cast<int>(*beats);
        if (*beats < 1.0 || *beats > 32.0 || ! isWhole(*beats))
            r.fail("val:beatsPerBar must be a whole number from 1 to 32", song.id);
    }
    song.loudness = r.number(node, vocab::song::loudness, "val:loudness");
    if (song.loudness && (*song.loudness > 0.0 || *song.loudness < -60.0))
        r.fail("val:loudness is in LUFS and must be between -60 and 0", song.id);
    if (const auto tail = r.number(node, vocab::song::tailSeconds, "val:tailSeconds"))
    {
        song.tailSeconds = *tail;
        if (*tail < 0.0 || *tail > 120.0)
            r.fail("val:tailSeconds must be between 0 and 120", song.id);
    }
    if (const auto fade = r.number(node, vocab::song::fadeSeconds, "val:fadeSeconds"))
    {
        song.fadeSeconds = *fade;
        if (*fade < 0.0 || *fade > 60.0)
            r.fail("val:fadeSeconds must be between 0 and 60", song.id);
    }
    if (const auto room = r.number(node, vocab::song::roomAmount, "val:roomAmount"))
    {
        song.roomAmount = *room;
        if (*room < 0.0 || *room > 1.0)
            r.fail("val:roomAmount must be between 0 and 1", song.id);
    }
    if (const auto drive = r.number(node, vocab::song::drive, "val:drive"))
    {
        song.drive = *drive;
        if (*drive < 0.1 || *drive > 10.0)
            r.fail("val:drive must be between 0.1 and 10", song.id);
    }

    // -- sections, in index order with no gaps
    struct Indexed { int index; SongSection section; };
    std::vector<Indexed> found;
    for (const auto& s : store.objects(node, vocab::song::hasSection))
    {
        SongSection section;
        section.id = std::string(s.string());
        section.name = localName(section.id);
        section.label = r.text(s, vocab::rdfs::label);
        if (section.label.empty()) section.label = section.name;

        const auto index = r.number(s, vocab::song::index, "val:index");
        const auto bars = r.number(s, vocab::song::bars, "val:bars");
        if (! index) r.fail("section " + section.name + " has no val:index", section.id);
        if (! bars)  r.fail("section " + section.name + " has no val:bars", section.id);
        if (! index || ! bars)
            continue;
        if (*bars < 1.0 || ! isWhole(*bars))
            r.fail("section " + section.name + " has " + std::to_string(*bars) + " bars; it needs a whole number of at least 1", section.id);
        section.index = static_cast<int>(*index);
        section.bars = static_cast<int>(*bars);
        section.level = r.number(s, vocab::song::level, "val:level").value_or(0.0);
        if (section.level < -60.0 || section.level > 24.0)
            r.fail("section " + section.name + " has a level of " + std::to_string(section.level) + " dB; it must be between -60 and 24", section.id);
        found.push_back({section.index, section});
    }
    if (found.empty() && r.ok)
        r.fail("the song has no sections (val:hasSection)", song.id);
    std::sort(found.begin(), found.end(), [](const Indexed& a, const Indexed& b) { return a.index < b.index; });
    int bar = 0;
    for (std::size_t i = 0; i < found.size(); ++i)
    {
        if (found[i].index != static_cast<int>(i) + 1)
        {
            r.fail("section " + found[i].section.name + " is numbered " + std::to_string(found[i].index)
                       + " but should be " + std::to_string(i + 1) + ": numbers run from 1 with no gaps and none twice",
                   found[i].section.id);
            break;
        }
        found[i].section.firstBar = bar;
        bar += found[i].section.bars;
        song.sections.push_back(found[i].section);
    }

    // -- parts, then returns
    std::set<std::string> names;
    const auto readStems = [&](const std::string& predicate, bool isReturn)
    {
        struct Ordered { double order; SongStem stem; };
        std::vector<Ordered> list;
        for (const auto& p : store.objects(node, predicate))
        {
            SongStem stem;
            stem.id = std::string(p.string());
            stem.name = localName(stem.id);
            stem.isReturn = isReturn;
            const std::string kind = isReturn ? "return " : "part ";
            stem.circuit = r.text(p, vocab::song::circuit);
            if (stem.circuit.empty())
                r.fail(kind + stem.name + " has no val:circuit", stem.id);
            if (isReturn)
            {
                const auto source = store.object(p, vocab::song::source);
                if (! source)
                    r.fail("return " + stem.name + " has no val:source", stem.id);
                else
                    stem.source = localName(std::string(source.string()));
            }
            else
            {
                stem.midi = r.text(p, vocab::song::midi);
                if (stem.midi.empty()) stem.midi = "midi/" + stem.name + ".mid";
            }
            stem.level = r.number(p, vocab::song::level, "val:level").value_or(stem.level);
            if (stem.level < -80.0 || stem.level > 0.0)
                r.fail(kind + stem.name + " has a level of " + std::to_string(stem.level) + " dB; it must be between -80 and 0", stem.id);
            stem.pan = r.number(p, vocab::song::pan, "val:pan").value_or(0.0);
            if (stem.pan < -1.0 || stem.pan > 1.0)
                r.fail(kind + stem.name + " has a pan of " + std::to_string(stem.pan) + "; it must be between -1 (left) and 1 (right)", stem.id);
            stem.highpass = r.number(p, vocab::song::highpass, "val:highpass").value_or(0.0);
            if (stem.highpass < 0.0 || stem.highpass > 2000.0)
                r.fail(kind + stem.name + " has a high-pass of " + std::to_string(stem.highpass) + " Hz; it must be between 0 and 2000", stem.id);
            if (! names.insert(stem.name).second)
                r.fail("two stems are called " + stem.name + "; the name is the local name of the IRI and must be unique", stem.id);

            const double order = r.number(p, vocab::song::index, "val:index").value_or(1.0e9);
            list.push_back({order, std::move(stem)});
        }
        std::sort(list.begin(), list.end(), [](const Ordered& a, const Ordered& b)
                  { return a.order < b.order || (! (b.order < a.order) && a.stem.name < b.stem.name); });
        for (auto& item : list)
            song.stems.push_back(std::move(item.stem));
    };
    readStems(vocab::song::hasPart, false);
    readStems(vocab::song::hasReturn, true);

    if (song.stems.empty() && r.ok)
        r.fail("the song has no parts (val:hasPart)", song.id);
    for (const auto& s : song.stems)
        if (s.isReturn)
        {
            const auto* source = song.findStem(s.source);
            if (source == nullptr || source->isReturn)
                r.fail("return " + s.name + " takes its sound from '" + s.source + "', which is not a part of this song", s.id);
        }

    if (! r.ok)
        return false;
    out = std::move(song);
    return true;
}

} // namespace valis

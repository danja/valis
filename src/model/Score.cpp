// src/model/Score.cpp
#include "valis/Score.h"

#include "valis/Vocabulary.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace valis {

namespace {

std::string localName(const std::string& iri)
{
    const auto at = iri.find_last_of("#/");
    return at == std::string::npos ? iri : iri.substr(at + 1);
}

std::vector<std::string> words(const std::string& text)
{
    std::istringstream in(text);
    std::vector<std::string> out;
    for (std::string w; in >> w;)
        out.push_back(w);
    return out;
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

std::string stripped(const std::string& steps)
{
    std::string out;
    for (const char c : steps)
        if (c != ' ' && c != '|' && c != '\t' && c != '\n')
            out += c;
    return out;
}

} // namespace

const ScoreFeel* Score::feelOf(const std::string& part) const
{
    for (const auto& f : feels)
        if (f.part == part) return &f;
    return nullptr;
}

bool Score::read(const rdf::TurtleStore& store, const Song& song, Score& out, std::vector<Diagnostic>& diagnostics)
{
    Reader r{store, diagnostics};
    Score score;

    const auto scores = store.subjectsOfType(vocab::score::Score);
    if (scores.size() != 1)
    {
        diagnostics.push_back({scores.empty() ? "there is no val:Score in this file"
                                              : "a file holds one val:Score; this one holds " + std::to_string(scores.size()), {}});
        return false;
    }
    const auto node = scores.front();
    score.id = std::string(node.string());

    if (const auto seed = r.number(node, vocab::score::seed, "val:seed"))
        score.seed = static_cast<long long>(*seed);
    if (const auto per = r.number(node, vocab::score::barsPerChord, "val:barsPerChord"))
    {
        score.barsPerChord = static_cast<int>(*per);
        if (*per < 1.0 || *per > 64.0 || std::floor(*per) != *per)
            r.fail("val:barsPerChord must be a whole number from 1 to 64", score.id);
    }
    score.progression = words(r.text(node, vocab::score::progression));

    // -- chords
    for (const auto& c : store.objects(node, vocab::score::hasChord))
    {
        ScoreChord chord;
        chord.name = localName(std::string(c.string()));
        if (const auto root = r.number(c, vocab::score::root, "val:root"))
            chord.root = static_cast<int>(*root);
        else
            r.fail("chord " + chord.name + " has no val:root", std::string(c.string()));
        for (const auto& w : words(r.text(c, vocab::score::tones)))
        {
            try { chord.tones.push_back(std::stoi(w)); }
            catch (...) { r.fail("chord " + chord.name + " has '" + w + "' among its val:tones; they are MIDI keys", std::string(c.string())); }
        }
        if (chord.tones.empty())
            r.fail("chord " + chord.name + " has no val:tones", std::string(c.string()));
        for (const int k : chord.tones)
            if (k < 0 || k > 127)
                r.fail("chord " + chord.name + " has the key " + std::to_string(k) + "; keys run from 0 to 127", std::string(c.string()));
        if (chord.root < 0 || chord.root > 127)
            r.fail("chord " + chord.name + " has the root " + std::to_string(chord.root) + "; keys run from 0 to 127", std::string(c.string()));
        std::sort(chord.tones.begin(), chord.tones.end());
        score.chords[chord.name] = chord;
    }
    for (const auto& name : score.progression)
        if (score.chords.count(name) == 0)
            r.fail("the progression names the chord '" + name + "', which the score does not declare (val:hasChord)", score.id);

    // -- patterns
    const int beatsPerBar = song.beatsPerBar;
    for (const auto& p : store.objects(node, vocab::score::hasPattern))
    {
        ScorePattern pattern;
        pattern.name = localName(std::string(p.string()));
        if (const auto bars = r.number(p, vocab::song::bars, "val:bars"))
        {
            pattern.bars = static_cast<int>(*bars);
            if (*bars < 1.0 || *bars > 64.0 || std::floor(*bars) != *bars)
                r.fail("pattern " + pattern.name + " has val:bars " + std::to_string(*bars) + "; it needs a whole number from 1 to 64", std::string(p.string()));
        }
        if (const auto spb = r.number(p, vocab::score::stepsPerBeat, "val:stepsPerBeat"))
        {
            pattern.stepsPerBeat = static_cast<int>(*spb);
            if (*spb < 1.0 || *spb > 12.0 || std::floor(*spb) != *spb)
                r.fail("pattern " + pattern.name + " has val:stepsPerBeat " + std::to_string(*spb) + "; it needs a whole number from 1 to 12", std::string(p.string()));
        }
        const std::size_t expected = static_cast<std::size_t>(std::max(1, pattern.bars) * beatsPerBar * std::max(1, pattern.stepsPerBeat));
        for (const auto& l : store.objects(p, vocab::score::hasLane))
        {
            ScoreLane lane;
            const std::string subject = std::string(l.string());
            if (const auto key = r.number(l, vocab::score::key, "val:key"))
            {
                lane.key = static_cast<int>(*key);
                if (*key < 0.0 || *key > 127.0)
                    r.fail("pattern " + pattern.name + " has the key " + std::to_string(*key) + "; keys run from 0 to 127", subject);
            }
            lane.voice = r.text(l, vocab::score::voice);
            if (lane.key < 0 && lane.voice.empty())
                r.fail("a lane of pattern " + pattern.name + " has neither val:key nor val:voice", subject);
            if (lane.key >= 0 && ! lane.voice.empty())
                r.fail("a lane of pattern " + pattern.name + " has both val:key and val:voice", subject);
            if (! lane.voice.empty() && lane.voice != "chord" && lane.voice != "root" && lane.voice != "top" && lane.voice != "low")
                r.fail("a lane of pattern " + pattern.name + " has the voice '" + lane.voice + "'; use chord, root, top or low", subject);
            if (! lane.voice.empty() && score.progression.empty())
                r.fail("pattern " + pattern.name + " takes a voice from the chord, and the score has no val:progression", subject);

            lane.steps = stripped(r.text(l, vocab::score::steps));
            for (const char c : lane.steps)
                if (c != 'x' && c != 'X' && c != '.' && c != '-')
                {
                    r.fail("a lane of pattern " + pattern.name + " has '" + std::string(1, c) + "' among its steps; use x, X, . or -", subject);
                    break;
                }
            if (lane.steps.size() != expected)
                r.fail("a lane of pattern " + pattern.name + " has " + std::to_string(lane.steps.size()) + " steps; "
                           + std::to_string(pattern.bars) + " bar(s) of " + std::to_string(beatsPerBar) + " beats at "
                           + std::to_string(pattern.stepsPerBeat) + " per beat need " + std::to_string(expected), subject);

            lane.velocity = static_cast<int>(r.number(l, vocab::score::velocity, "val:velocity").value_or(lane.velocity));
            if (lane.velocity < 1 || lane.velocity > 127)
                r.fail("a lane of pattern " + pattern.name + " has the velocity " + std::to_string(lane.velocity) + "; it runs from 1 to 127", subject);
            lane.accent = static_cast<int>(r.number(l, vocab::score::accent, "val:accent").value_or(lane.accent));
            lane.length = r.number(l, vocab::score::length, "val:length").value_or(lane.length);
            if (lane.length <= 0.0 || lane.length > 256.0)
                r.fail("a lane of pattern " + pattern.name + " has val:length " + std::to_string(lane.length) + "; it is in steps and must be above 0", subject);
            lane.gate = r.number(l, vocab::score::gate, "val:gate").value_or(lane.gate);
            if (lane.gate <= 0.0 || lane.gate > 1.0)
                r.fail("a lane of pattern " + pattern.name + " has val:gate " + std::to_string(lane.gate) + "; it must be above 0 and at most 1", subject);
            lane.octave = static_cast<int>(r.number(l, vocab::score::octave, "val:octave").value_or(0.0));
            lane.transpose = static_cast<int>(r.number(l, vocab::score::transpose, "val:transpose").value_or(0.0));
            pattern.lanes.push_back(std::move(lane));
        }
        if (pattern.lanes.empty())
            r.fail("pattern " + pattern.name + " has no lanes (val:hasLane)", std::string(p.string()));
        score.patterns[pattern.name] = std::move(pattern);
    }

    // -- how each part is played
    for (const auto& f : store.objects(node, vocab::score::hasFeel))
    {
        ScoreFeel feel;
        feel.part = r.text(f, vocab::score::part);
        const std::string subject = std::string(f.string());
        const auto* stem = song.findStem(feel.part);
        if (stem == nullptr || stem->isReturn)
            r.fail("a val:Feel names the part '" + feel.part + "', which is not a part of the song", subject);
        feel.timingMs = r.number(f, vocab::score::timing, "val:timing").value_or(0.0);
        feel.velocity = r.number(f, vocab::score::velocity, "val:velocity").value_or(0.0);
        feel.swing = r.number(f, vocab::score::swing, "val:swing").value_or(0.5);
        if (feel.timingMs < 0.0 || feel.timingMs > 200.0)
            r.fail("the feel of " + feel.part + " has val:timing " + std::to_string(feel.timingMs) + " ms; it must be between 0 and 200", subject);
        if (feel.velocity < 0.0 || feel.velocity > 60.0)
            r.fail("the feel of " + feel.part + " has val:velocity " + std::to_string(feel.velocity) + "; it must be between 0 and 60", subject);
        if (feel.swing < 0.5 || feel.swing > 0.75)
            r.fail("the feel of " + feel.part + " has val:swing " + std::to_string(feel.swing) + "; it runs from 0.5 (straight) to 0.75", subject);
        score.feels.push_back(std::move(feel));
    }

    // -- the plan
    for (const auto& pl : store.objects(node, vocab::score::hasPlay))
    {
        ScorePlay play;
        const std::string subject = std::string(pl.string());
        play.section = r.text(pl, vocab::score::section);
        play.part = r.text(pl, vocab::score::part);
        play.pattern = localName(r.text(pl, vocab::score::pattern));
        play.progression = r.text(pl, vocab::score::progression);

        const SongSection* section = nullptr;
        for (const auto& s : song.sections)
            if (s.name == play.section) section = &s;
        if (section == nullptr)
            r.fail("a val:Play names the section '" + play.section + "', which is not a section of the song", subject);
        const auto* stem = song.findStem(play.part);
        if (stem == nullptr || stem->isReturn)
            r.fail("a val:Play names the part '" + play.part + "', which is not a part of the song", subject);
        if (score.patterns.count(play.pattern) == 0)
            r.fail("a val:Play of " + play.part + " in " + play.section + " names the pattern '" + play.pattern + "', which the score does not declare", subject);

        play.fromBar = static_cast<int>(r.number(pl, vocab::score::fromBar, "val:fromBar").value_or(1.0));
        play.toBar = static_cast<int>(r.number(pl, vocab::score::toBar, "val:toBar").value_or(0.0));
        play.every = static_cast<int>(r.number(pl, vocab::score::every, "val:every").value_or(0.0));
        if (section != nullptr)
        {
            if (play.fromBar < 1 || play.fromBar > section->bars)
                r.fail("a val:Play in " + play.section + " starts at bar " + std::to_string(play.fromBar) + "; the section has " + std::to_string(section->bars), subject);
            if (play.toBar != 0 && (play.toBar < play.fromBar || play.toBar > section->bars))
                r.fail("a val:Play in " + play.section + " ends at bar " + std::to_string(play.toBar) + ", outside bars " + std::to_string(play.fromBar) + " to " + std::to_string(section->bars), subject);
        }
        if (play.every < 0)
            r.fail("a val:Play has val:every " + std::to_string(play.every) + "; it must be 1 or more", subject);
        for (const auto& name : words(play.progression))
            if (score.chords.count(name) == 0)
                r.fail("a val:Play names the chord '" + name + "', which the score does not declare", subject);
        score.plays.push_back(std::move(play));
    }

    // a voice lane in a pattern needs a progression for every play that uses it
    for (const auto& pl : score.plays)
    {
        const auto found = score.patterns.find(pl.pattern);
        if (found == score.patterns.end() || ! score.progression.empty() || ! pl.progression.empty())
            continue;
        for (const auto& lane : found->second.lanes)
            if (! lane.voice.empty())
                r.fail("a val:Play of " + pl.part + " in " + pl.section + " uses a voice lane and has no progression", score.id);
    }

    if (score.plays.empty() && r.ok)
        r.fail("the score plays nothing (val:hasPlay)", score.id);
    if (! r.ok)
        return false;
    out = std::move(score);
    return true;
}

} // namespace valis

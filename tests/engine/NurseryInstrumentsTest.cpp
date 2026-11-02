// tests/engine/NurseryInstrumentsTest.cpp
//
// The eight nursery-rhyme instruments (scripts/nursery/nursery.py), loaded and played through the engine as
// the plugin would: silent until played, in range, in tune where they are pitched, quiet after release, and
// each doing the thing it was built for (the kick falls, a hard hat rings longer than a soft one, the riser
// builds).

#include "valis/CircuitCompiler.h"
#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/TurtleStore.h"
#include "valis/ValisEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct Rig
{
    Ontology ontology;
    ElementRegistry registry = makeDefaultRegistry();
    ValisEngine engine;
    CircuitModel model;

    explicit Rig(const std::string& name)
    {
        std::vector<std::string> errors;
        ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool loadedOntology = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(loadedOntology);

        std::ifstream file(std::string(VALIS_EXAMPLES_DIR) + "/" + name);
        std::ostringstream text;
        text << file.rdbuf();
        assert(! text.str().empty());

        std::vector<Diagnostic> diagnostics;
        rdf::TurtleStore store;
        std::vector<rdf::ParseError> parseErrors;
        const bool parsed = store.parse(text.str(), "urn:valis:circuit", parseErrors);
        assert(parsed);

        const bool built = model.build(store, ontology, diagnostics);
        for (const auto& d : diagnostics)
            std::printf("  %s: %s\n", name.c_str(), d.message.c_str());
        assert(built);

        CompiledCircuit compiled;
        CircuitCompiler compiler;
        const bool compiledOk = compiler.compile(model, ontology, compiled, diagnostics);
        assert(compiledOk);

        // Prepared before the circuit is installed, so its elements are built
        // for this rate.
        engine.prepare(kRate, kBlock);

        std::string error;
        const bool installed = engine.load(compiled, registry, error);
        if (! installed)
            std::printf("  %s: %s\n", name.c_str(), error.c_str());
        assert(installed);

    }

    /// Renders `seconds` and returns the mono output.
    std::vector<float> render(double seconds)
    {
        std::vector<float> all;
        std::vector<float> block(kBlock, 0.0f);
        for (int i = 0; i < static_cast<int>(seconds * kRate / kBlock); ++i)
        {
            engine.process(nullptr, block.data(), kBlock);
            all.insert(all.end(), block.begin(), block.end());
        }
        return all;
    }
};

float peak(const std::vector<float>& s, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    float p = 0.0f;
    for (std::size_t i = from; i < std::min(to, s.size()); ++i)
    {
        assert(std::isfinite(s[i]));
        p = std::max(p, std::abs(s[i]));
    }
    return p;
}

double amplitudeAt(const std::vector<float>& s, std::size_t from, std::size_t length, double hz)
{
    assert(from + length <= s.size());

    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < length; ++i)
    {
        const double phase = 6.283185307179586 * hz * static_cast<double>(i) / kRate;
        re += s[from + i] * std::cos(phase);
        im += s[from + i] * std::sin(phase);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(length);
}

const char* const kAll[] = { "nursery-humpty-kick.ttl", "nursery-black-sheep-bass.ttl", "nursery-twinkle-lead.ttl",
                             "nursery-incy-wincy-arp.ttl", "nursery-rock-a-bye-pad.ttl", "nursery-rattle-snare.ttl",
                             "nursery-ding-dong-hat.ttl", "nursery-wind-up-riser.ttl" };

float rms(const std::vector<float>& s, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < std::min(to, s.size()); ++i)
        sum += static_cast<double>(s[i]) * s[i];
    return static_cast<float>(std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, to - from))));
}

int crossings(const std::vector<float>& s, std::size_t from, std::size_t to)
{
    int n = 0;
    for (std::size_t i = from + 1; i < std::min(to, s.size()); ++i)
        if ((s[i - 1] < 0.0f) != (s[i] < 0.0f)) ++n;
    return n;
}

bool isTunedTo(const std::vector<float>& s, double hz)
{
    return amplitudeAt(s, 12000, 24000, hz) > amplitudeAt(s, 12000, 24000, hz * 1.035) * 2.0;
}

void testSilentUntilPlayedAndInRange()
{
    for (const char* name : kAll)
    {
        Rig rig(name);
        const float idle = peak(rig.render(0.3));
        if (idle >= 0.001f)
            std::fprintf(stderr, "  %s: not silent before a note, peak %g\n", name, idle);
        assert(idle < 0.001f);

        rig.engine.noteOn(57, 1.0f);
        const float p = peak(rig.render(1.0));
        std::printf("  %s: peak %.3f\n", name, p);
        assert(p > 0.03f);
        assert(p < 1.0f);
    }
}

void testFallsSilentAfterRelease()
{
    for (const char* name : kAll)
    {
        Rig rig(name);
        rig.engine.noteOn(52, 1.0f);
        rig.render(0.4);
        rig.engine.noteOff(52);
        const auto tail = rig.render(16.0);
        assert(peak(tail, tail.size() - 4800) < 0.001f);
    }
}

void testPitchedInstrumentsAreInTune()
{
    struct Case { const char* name; int note; double hz; };
    const Case cases[] = { { "nursery-black-sheep-bass.ttl", 45, 110.0 },  { "nursery-twinkle-lead.ttl", 69, 440.0 },
                           { "nursery-incy-wincy-arp.ttl", 57, 220.0 },     { "nursery-incy-wincy-arp.ttl", 69, 440.0 },
                           { "nursery-rock-a-bye-pad.ttl", 45, 110.0 } };
    for (const auto& c : cases)
    {
        Rig rig(c.name);
        rig.engine.noteOn(c.note, 1.0f);
        const auto held = rig.render(1.0);
        if (! isTunedTo(held, c.hz))
            std::fprintf(stderr, "  %s: note %d is not tuned to %g Hz\n", c.name, c.note, c.hz);
        assert(isTunedTo(held, c.hz));
    }
}

void testKickFalls()
{
    Rig rig("nursery-humpty-kick.ttl");
    rig.engine.noteOn(36, 1.0f);
    const auto k = rig.render(0.6);
    const int early = crossings(k, 480, 1920);        // 10 to 40 ms
    const int late  = crossings(k, 9600, 12480);      // 200 to 260 ms, the same width of 1440 samples scaled
    const double earlyRate = early / 0.030, lateRate = late / 0.060;
    std::printf("  kick: %.0f crossings per second early, %.0f late\n", earlyRate, lateRate);
    assert(earlyRate > lateRate * 1.5);
    assert(rms(k, 0, 4800) > 0.1f);
}

void testHatVelocityChoosesOpenOrClosed()
{
    float tail[2] = {};
    int at = 0;
    for (const float velocity : { 0.15f, 1.0f })
    {
        Rig rig("nursery-ding-dong-hat.ttl");
        rig.engine.noteOn(42, velocity);
        const auto hat = rig.render(0.6);
        tail[at++] = rms(hat, 7200, 14400);               // 150 to 300 ms
    }
    std::printf("  hat: soft tail %.5f, hard tail %.5f\n", tail[0], tail[1]);
    assert(tail[1] > tail[0] * 4.0f);
}

void testRiserBuilds()
{
    Rig rig("nursery-wind-up-riser.ttl");
    rig.engine.noteOn(60, 1.0f);
    const auto r = rig.render(5.5);
    const float start = rms(r, 9600, 33600);              // 0.2 to 0.7 s
    const float top   = rms(r, 216000, 240000);           // 4.5 to 5.0 s
    std::printf("  riser: rms %.5f at the start, %.5f near the top\n", start, top);
    assert(top > start * 3.0f);
}

}  // namespace

int main()
{
    testSilentUntilPlayedAndInRange();
    testFallsSilentAfterRelease();
    testPitchedInstrumentsAreInTune();
    testKickFalls();
    testHatVelocityChoosesOpenOrClosed();
    testRiserBuilds();
    std::printf("NurseryInstrumentsTest passed\n");
    return 0;
}

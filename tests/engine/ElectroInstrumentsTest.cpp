// tests/engine/ElectroInstrumentsTest.cpp
//
// The instruments built for the 1983-style track (docs/new-order-track.md), loaded and
// played through the engine as the plugin would: silent until played, at the pitch of the
// note, finite and in range, and quiet again after release. The speaker and room effect is
// held to passing a burst and to staying silent on silence.

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

const char* const kInstruments[] = { "moog-source.ttl", "six-string-bass.ttl", "prophet-5.ttl",
                                     "emulator-choir.ttl", "quadra-strings.ttl" };

/// Whether the note's own frequency stands out from a pitch a little off it.
bool isTunedTo(const std::vector<float>& s, double hz)
{
    const double at   = amplitudeAt(s, 12000, 24000, hz);
    const double near = amplitudeAt(s, 12000, 24000, hz * 1.035);
    return at > near * 2.0;
}

void testEachInstrumentPlaysInTune()
{
    for (const char* name : kInstruments)
    {
        Rig rig(name);
        const float idle = peak(rig.render(0.2));
        if (idle >= 0.001f)
            std::fprintf(stderr, "  %s: not silent before a note, peak %g\n", name, idle);
        assert(idle < 0.001f);                         // silent until played; the choir's tape has a little hiss

        rig.engine.noteOn(45, 1.0f);                   // A2, 110 Hz
        const auto held = rig.render(1.0);
        const float p = peak(held);
        std::printf("  %s: peak %.3f\n", name, p);
        assert(p > 0.02f);
        assert(p < 1.0f);
        assert(isTunedTo(held, 110.0));
    }
}

void testEachInstrumentFallsSilentAfterRelease()
{
    for (const char* name : kInstruments)
    {
        Rig rig(name);
        rig.engine.noteOn(52, 1.0f);
        rig.render(0.4);
        rig.engine.noteOff(52);
        const auto tail = rig.render(14.0);
        assert(peak(tail, tail.size() - 4800) < 0.001f);
    }
}

/// A new note after a short gap speaks again; the bass guitar's envelope has to
/// restart for each pluck, which is why the score leaves a gap between notes.
void testEachNoteAfterAGapSpeaksAgain()
{
    for (const char* name : kInstruments)
    {
        Rig rig(name);
        rig.engine.noteOn(57, 1.0f);
        rig.render(0.3);
        rig.engine.noteOff(57);
        rig.render(0.05);
        rig.engine.noteOn(64, 1.0f);
        const auto second = rig.render(0.5);
        assert(peak(second) > 0.02f);
    }
}

void testSpeakerRoomPassesABurstAndStaysSilentOnSilence()
{
    Rig quiet("speaker-room.ttl");
    std::vector<float> in(kBlock, 0.0f), out(kBlock, 0.0f);
    for (int i = 0; i < 20; ++i)
    {
        quiet.engine.process(in.data(), out.data(), kBlock);
        assert(peak(out) == 0.0f);
    }

    Rig rig("speaker-room.ttl");
    float loudest = 0.0f;
    float room = 0.0f;
    for (int block = 0; block < 40; ++block)
    {
        std::fill(in.begin(), in.end(), 0.0f);
        if (block == 0)
            for (int i = 0; i < 64; ++i)
                in[static_cast<std::size_t>(i)] = std::sin(0.1 * i);          // a short low burst
        rig.engine.process(in.data(), out.data(), kBlock);
        loudest = std::max(loudest, peak(out));
        if (block >= 10)
            room = std::max(room, peak(out));
    }
    std::printf("  speaker-room: burst peak %.3f, tail peak %.5f\n", loudest, room);
    assert(loudest > 0.01f);
    assert(loudest < 1.0f);
    assert(room < loudest);                            // the room dies away
}

}  // namespace

int main()
{
    testEachInstrumentPlaysInTune();
    testEachInstrumentFallsSilentAfterRelease();
    testEachNoteAfterAGapSpeaksAgain();
    testSpeakerRoomPassesABurstAndStaysSilentOnSilence();
    std::printf("ElectroInstrumentsTest passed\n");
    return 0;
}

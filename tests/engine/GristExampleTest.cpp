// tests/engine/GristExampleTest.cpp
//
// examples/grist.ttl through the engine: a grain delay whose output is fed back
// into the history it records, closed through a val:UnitDelay. The properties
// that matter are that it sounds, that the loop can be turned right up without
// running away, and that the feedback is what makes a tail.

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

    Rig()
    {
        std::vector<std::string> errors;
        ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool loaded = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(loaded);

        std::ifstream file(VALIS_EXAMPLES_DIR "/grist.ttl");
        std::ostringstream text;
        text << file.rdbuf();

        rdf::TurtleStore store;
        std::vector<rdf::ParseError> parseErrors;
        const bool parsed = store.parse(text.str(), "urn:valis:circuit", parseErrors);
        assert(parsed);

        std::vector<Diagnostic> diagnostics;
        const bool built = model.build(store, ontology, diagnostics);
        for (const auto& d : diagnostics)
            std::printf("  %s\n", d.message.c_str());
        assert(built);

        CompiledCircuit compiled;
        CircuitCompiler compiler;
        const bool compiledOk = compiler.compile(model, ontology, compiled, diagnostics);
        assert(compiledOk);

        engine.prepare(kRate, kBlock);
        std::string error;
        const bool installed = engine.load(compiled, registry, error);
        assert(installed);
    }

    void setParam(const std::string& symbol, float value)
    {
        for (const auto& binding : model.params())
        {
            if (binding.symbol != symbol)
                continue;

            engine.setControl(binding.targetNode, binding.propertySymbol, value);
            for (const auto& [node, port] : binding.alsoTargets)
                engine.setControl(node, port, value);
            return;
        }
        assert(false && "no such parameter");
    }

    /// Plays `seconds` of a 440 Hz tone, or silence, and returns the left channel.
    std::vector<float> play(double seconds, bool tone)
    {
        std::vector<float> out;
        std::vector<float> in(kBlock), left(kBlock), right(kBlock);
        for (int b = 0; b < static_cast<int>(seconds * kRate / kBlock); ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                in[static_cast<std::size_t>(i)] = tone
                    ? 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0
                          * static_cast<double>(position++) / kRate))
                    : 0.0f;
            engine.process(in.data(), left.data(), right.data(), kBlock);
            out.insert(out.end(), left.begin(), left.end());
        }
        return out;
    }

    long position = 0;
};

float rms(const std::vector<float>& s, std::size_t from)
{
    double sum = 0.0;
    for (std::size_t i = from; i < s.size(); ++i)
    {
        assert(std::isfinite(s[i]));
        sum += static_cast<double>(s[i]) * s[i];
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(s.size() - from)));
}

float peak(const std::vector<float>& s)
{
    float p = 0.0f;
    for (const float v : s)
        p = std::max(p, std::abs(v));
    return p;
}

void testItSoundsAndHasControls()
{
    Rig rig;
    assert(rig.model.params().size() >= 20);

    const auto out = rig.play(1.0, true);
    assert(rms(out, 24000) > 0.05f);
    assert(peak(out) < 1.5f);
}

void testFeedbackMakesATail()
{
    // Tone for a second, then nothing. With the loop off the grains die away
    // within the history; with it up there is still sound well after.
    const auto tail = [](float feedbackDb)
    {
        Rig rig;
        rig.setParam("feedback", feedbackDb);
        rig.setParam("delay", 150.0f);
        rig.play(1.0, true);
        const auto quiet = rig.play(4.0, false);
        return rms(quiet, quiet.size() - 24000);
    };

    assert(tail(-60.0f) < 1.0e-4f);
    assert(tail(-3.0f) > 100.0f * tail(-60.0f));
}

void testTheFeedbackFilterHasThreeTypes()
{
    // The same tone fed round the loop, with a corner at 2 kHz: a lowpass lets the tail
    // through and a highpass takes it away, because the tone is well below the corner.
    const auto tail = [](float type)
    {
        Rig rig;
        rig.setParam("feedback", -3.0f);
        rig.setParam("delay", 150.0f);
        rig.setParam("tone", 2000.0f);
        rig.setParam("filterMode", type);
        rig.play(1.0, true);
        const auto quiet = rig.play(2.0, false);
        return rms(quiet, quiet.size() - 24000);
    };

    const float low = tail(0.0f), band = tail(1.0f), high = tail(2.0f);
    std::printf("  feedback tail: lowpass %.3e, bandpass %.3e, highpass %.3e\n", low, band, high);
    assert(low > 1.0e-7f);
    assert(low > 10.0f * high);
    assert(low > band);
}

void testTheLoopCanBeTurnedFullyUp()
{
    // A loop gain of one, with the grains and the limiter in it: loud, but it
    // must settle rather than grow without bound or turn to NaN.
    Rig rig;
    rig.setParam("feedback", 0.0f);
    rig.setParam("delay", 80.0f);
    const auto out = rig.play(6.0, true);
    assert(peak(out) < 4.0f);
    for (const float v : out)
        assert(std::isfinite(v));
}

void testTempoSyncReachesTheGranulator()
{
    const auto energy = [](bool sync)
    {
        Rig rig;
        rig.setParam("sizeSync", sync ? 1.0f : 0.0f);
        rig.setParam("sizeDiv", 0.03125f);        // 1/128 of a bar: about 15 ms at 120 bpm
        return rig.play(1.0, true);
    };

    // A different grain length is a different render.
    assert(energy(true) != energy(false));
}

}  // namespace

int main()
{
    testItSoundsAndHasControls();
    testFeedbackMakesATail();
    testTheFeedbackFilterHasThreeTypes();
    testTheLoopCanBeTurnedFullyUp();
    testTempoSyncReachesTheGranulator();
    std::puts("GristExampleTest passed");
    return 0;
}

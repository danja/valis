// tests/engine/DemoEffectsTest.cpp
//
// examples/split-reverb.ttl and examples/lofi.ttl through the engine. The
// elements have their own tests; these check that the circuits built from them
// do what their headers say: the split reverb leaves the low band dry and
// rings on in the high one, the lo-fi chain degrades and still holds its
// ceiling, both repeat exactly because Drift is seeded, and the multiband adds
// back to a flat response and lifts quiet material by a limited amount.

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

constexpr double kRate  = 48000.0;
constexpr double kPi    = 3.14159265358979323846;
constexpr int    kBlock = 512;

struct Rig
{
    Ontology ontology;
    ElementRegistry registry = makeDefaultRegistry();
    ValisEngine engine;
    CircuitModel model;
    long position = 0;

    explicit Rig(const char* file)
    {
        std::vector<std::string> errors;
        ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
        const bool loaded = ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(loaded);

        std::ifstream stream(std::string(VALIS_EXAMPLES_DIR) + "/" + file);
        assert(stream.good());
        std::ostringstream text;
        text << stream.rdbuf();

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
            if (binding.symbol == symbol)
            {
                engine.setControl(binding.targetNode, binding.propertySymbol, value);
                for (const auto& [node, port] : binding.alsoTargets)
                    engine.setControl(node, port, value);
                return;
            }
        assert(false && "no such parameter");
    }

    /// Plays `seconds` of a sine (or silence if the frequency is 0) and returns
    /// the left channel.
    std::vector<float> play(double seconds, double hz, float amplitude)
    {
        std::vector<float> out;
        std::vector<float> in(kBlock), left(kBlock), right(kBlock);
        for (int b = 0; b < static_cast<int>(seconds * kRate / kBlock); ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                in[static_cast<std::size_t>(i)] = hz > 0.0
                    ? amplitude * static_cast<float>(std::sin(2.0 * kPi * hz
                          * static_cast<double>(position++) / kRate))
                    : 0.0f;
            engine.process(in.data(), left.data(), right.data(), kBlock);
            out.insert(out.end(), left.begin(), left.end());
        }
        return out;
    }
};

float rms(const std::vector<float>& s, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        assert(std::isfinite(s[i]));
        sum += static_cast<double>(s[i]) * s[i];
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(to - from)));
}

float peak(const std::vector<float>& s)
{
    float p = 0.0f;
    for (const float v : s)
        p = std::max(p, std::abs(v));
    return p;
}

void testSplitReverbLeavesTheLowBandDry()
{
    Rig rig("split-reverb.ttl");
    rig.setParam("reverb", 1.0f);

    // 80 Hz is well under the 300 Hz split: it should come through at close to
    // its own level, with none of the reverb's build-up.
    const auto out = rig.play(2.0, 80.0, 0.25f);
    const float level = rms(out, 48000, out.size());
    std::printf("  80 Hz in at %.3f rms, out at %.3f\n", 0.25f / std::sqrt(2.0f), level);
    assert(std::abs(level / (0.25f / std::sqrt(2.0f)) - 1.0f) < 0.15f);
}

void testSplitReverbRingsOnInTheHighBand()
{
    const auto tailAfter = [](float reverb)
    {
        Rig rig("split-reverb.ttl");
        rig.setParam("reverb", reverb);
        rig.play(0.5, 2000.0, 0.25f);
        const auto quiet = rig.play(1.5, 0.0, 0.0f);
        return rms(quiet, 24000, 48000);   // 0.5 to 1 s after the tone stops
    };

    const float dry = tailAfter(0.0f);
    const float wet = tailAfter(1.0f);
    std::printf("  tail 0.5 s after the tone: dry %.2g, reverb %.2g\n", dry, wet);
    assert(dry < 1.0e-4f);
    assert(wet > 100.0f * dry + 1.0e-3f);
}

void testSplitReverbRepeatsAndStaysUnderTheCeiling()
{
    Rig a("split-reverb.ttl");
    Rig b("split-reverb.ttl");
    const auto first  = a.play(3.0, 440.0, 1.5f);
    const auto second = b.play(3.0, 440.0, 1.5f);
    assert(first == second);

    // The limiter is at -1 dB, 0.891, and the engine applies no gain after it.
    std::printf("  loud input peaks at %.4f (ceiling 0.8913)\n", peak(first));
    assert(peak(first) <= 0.8914f);
}

void testLofiDegradesAndHoldsItsCeiling()
{
    Rig rig("lofi.ttl");
    const auto out = rig.play(2.0, 1000.0, 1.2f);

    std::printf("  lo-fi peak %.4f (ceiling 0.8913)\n", peak(out));
    assert(peak(out) <= 0.8914f);
    assert(rms(out, 24000, out.size()) > 0.1f);

    // Ten bits and held samples: the output is not what the same circuit gives
    // with the degradation turned off.
    Rig clean("lofi.ttl");
    clean.setParam("bits", 24.0f);
    clean.setParam("downsample", 1.0f);
    const auto reference = clean.play(2.0, 1000.0, 1.2f);

    std::vector<float> difference(out.size());
    for (std::size_t i = 0; i < out.size(); ++i)
        difference[i] = out[i] - reference[i];
    std::printf("  difference from the clean chain: %.4f rms\n", rms(difference, 24000, out.size()));
    assert(rms(difference, 24000, out.size()) > 0.01f);
}

void testLofiRepeatsExactly()
{
    Rig a("lofi.ttl");
    Rig b("lofi.ttl");
    assert(a.play(2.0, 330.0, 0.5f) == b.play(2.0, 330.0, 0.5f));
}

void testLofiToneKnobDarkensIt()
{
    // The same bright tone through a low and a high tone setting: the high cut
    // at 1.5 kHz should take most of a 6 kHz tone; at 12 kHz, little of it.
    const auto level = [](float tone)
    {
        Rig rig("lofi.ttl");
        rig.setParam("wobble", 0.0f);
        rig.setParam("tone", tone);
        rig.setParam("bits", 24.0f);
        rig.setParam("downsample", 1.0f);
        const auto out = rig.play(1.0, 6000.0, 0.4f);
        return rms(out, 24000, out.size());
    };

    const float dark = level(1500.0f);
    const float open = level(12000.0f);
    std::printf("  6 kHz through tone 1.5 kHz: %.4f, through 12 kHz: %.4f\n", dark, open);
    assert(dark < 0.15f * open);
}

void testMultibandIsFlatWhenTheCompressorsDoNothing()
{
    // Ratio 1 and no upward stage: the compressors are wires, so what is left is
    // the crossovers and their sum, which must not colour the sound.
    for (const double hz : { 60.0, 150.0, 200.0, 260.0, 1000.0, 3000.0, 6000.0 })
    {
        Rig rig("multiband.ttl");
        rig.setParam("ratio", 1.0f);
        rig.setParam("upward", 0.0f);
        const auto out = rig.play(1.0, hz, 0.3f);
        const float gain = rms(out, 24000, out.size()) / (0.3f / std::sqrt(2.0f));
        std::printf("  %.0f Hz through three flat bands: %.3f\n", hz, gain);
        assert(std::abs(gain - 1.0f) < 0.04f);
    }
}

void testMultibandLiftsWhatIsQuiet()
{
    const auto level = [](float lift)
    {
        Rig rig("multiband.ttl");
        rig.setParam("upward", lift);
        const auto out = rig.play(1.5, 1000.0, 0.003f);     // -50 dB
        return rms(out, 36000, out.size());
    };

    const float flat = level(0.0f);
    const float lifted = level(1.0f);
    std::printf("  -50 dB tone: %.5f without lift, %.5f with\n", flat, lifted);
    assert(lifted > 2.0f * flat);                 // at least 6 dB
    assert(lifted < 5.0f * flat);                 // and under the 12 dB cap
}

void testMultibandHoldsItsCeiling()
{
    Rig rig("multiband.ttl");
    const auto out = rig.play(1.5, 200.0, 2.0f);
    std::printf("  hot input peaks at %.4f\n", peak(out));
    assert(peak(out) <= 0.8914f);
}

}  // namespace

int main()
{
    testSplitReverbLeavesTheLowBandDry();
    testSplitReverbRingsOnInTheHighBand();
    testSplitReverbRepeatsAndStaysUnderTheCeiling();
    testLofiDegradesAndHoldsItsCeiling();
    testLofiRepeatsExactly();
    testLofiToneKnobDarkensIt();
    testMultibandIsFlatWhenTheCompressorsDoNothing();
    testMultibandLiftsWhatIsQuiet();
    testMultibandHoldsItsCeiling();

    std::printf("DemoEffectsTest PASSED\n");
    return 0;
}

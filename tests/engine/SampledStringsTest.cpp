// tests/engine/SampledStringsTest.cpp
//
// examples/sampled-strings.ttl, loaded and played through the engine as the plugin would: three voices of recorded
// string sections chosen by one control. Checks that it is silent until played, that every voice is in tune across the
// keyboard (the recordings are stretched over the keys between them), that the voices are about as loud as each other,
// that a note outlasts its recording (the recordings are 9 to 15 seconds and loop), and that changing voice mid-note
// stays finite and keeps sounding.

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

    explicit Rig(const std::string& name = "sampled-strings.ttl")
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

double rms(const std::vector<float>& s, double fromSeconds, double toSeconds)
{
    const auto a = static_cast<std::size_t>(fromSeconds * kRate);
    const auto b = std::min(s.size(), static_cast<std::size_t>(toSeconds * kRate));
    assert(a < b);
    double sum = 0.0;
    for (std::size_t i = a; i < b; ++i)
    {
        assert(std::isfinite(s[i]));
        sum += static_cast<double>(s[i]) * s[i];
    }
    return std::sqrt(sum / static_cast<double>(b - a));
}

double db(double x) { return 20.0 * std::log10(x + 1.0e-12); }

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

/// The offset in cents from the played key at which the first four harmonics together peak over a second of the note,
/// searched 60 cents either way in steps of four. A recording stretched far from its own pitch has a weak
/// fundamental, so one partial alone is not a safe guide.
double centsOff(const std::vector<float>& s, int key)
{
    const double hz = 440.0 * std::pow(2.0, (key - 69) / 12.0);
    double best = -1.0, bestCents = 0.0;
    for (double cents = -60.0; cents <= 60.0; cents += 4.0)
    {
        double sum = 0.0;
        for (int h = 1; h <= 4; ++h)
            sum += amplitudeAt(s, static_cast<std::size_t>(0.6 * kRate), static_cast<std::size_t>(1.0 * kRate),
                               hz * h * std::pow(2.0, cents / 1200.0));
        if (sum > best) { best = sum; bestCents = cents; }
    }
    return bestCents;
}

void testSilentUntilPlayedAndEveryVoiceSounds()
{
    for (int voice = 0; voice < 3; ++voice)
    {
        Rig rig;
        rig.engine.setControl("sel", "select", static_cast<float>(voice));
        const auto quiet = rig.render(0.3);
        assert(rms(quiet, 0.0, 0.3) == 0.0);

        rig.engine.noteOn(60, 0.9f);
        const auto held = rig.render(2.0);
        assert(rms(held, 0.5, 2.0) > 0.01);
    }
}

/// Plays a key for `seconds`, lets it die away, and returns the held part. One rig serves many notes: loading
/// sixteen recordings is most of what a test here costs.
std::vector<float> playKey(Rig& rig, int key, double seconds)
{
    rig.engine.noteOn(key, 0.9f);
    auto held = rig.render(seconds);
    rig.engine.noteOff(key);
    rig.render(1.5);
    return held;
}

void testEveryVoiceIsInTuneAcrossTheKeyboard()
{
    const int keys[] = {38, 52, 64, 76, 90};
    for (int voice = 0; voice < 3; ++voice)
    {
        Rig rig;
        rig.engine.setControl("sel", "select", static_cast<float>(voice));
        for (const int key : keys)
        {
            const double off = centsOff(playKey(rig, key, 2.0), key);
            if (std::abs(off) > 25.0)
            {
                std::printf("  voice %d key %d is %.0f cents off\n", voice, key, off);
                std::fflush(stdout);
            }
            assert(std::abs(off) <= 25.0);
        }
    }
}

void testTheVoicesAreAboutAsLoudAsEachOther()
{
    // The recordings differ by 15 dB or more, so the table carries a gain per recording.
    double lowest[3] = {1.0e9, 1.0e9, 1.0e9}, highest[3] = {-1.0e9, -1.0e9, -1.0e9};
    const int keys[] = {48, 60, 72};
    for (int voice = 0; voice < 3; ++voice)
    {
        Rig rig;
        rig.engine.setControl("sel", "select", static_cast<float>(voice));
        for (int k = 0; k < 3; ++k)
        {
            const double level = db(rms(playKey(rig, keys[k], 2.0), 0.5, 2.0));
            lowest[k] = std::min(lowest[k], level);
            highest[k] = std::max(highest[k], level);
        }
    }
    for (int k = 0; k < 3; ++k)
    {
        if (highest[k] - lowest[k] > 5.0)
            std::printf("  key %d: voices differ by %.1f dB\n", keys[k], highest[k] - lowest[k]);
        assert(highest[k] - lowest[k] <= 5.0);
    }
}

void testANoteOutlastsItsRecording()
{
    for (int voice = 0; voice < 3; ++voice)
    {
        Rig rig;
        rig.engine.setControl("sel", "select", static_cast<float>(voice));
        rig.engine.noteOn(62, 0.9f);
        const auto note = rig.render(22.0);      // the longest recording is 15 seconds
        const double early = db(rms(note, 3.0, 6.0));
        const double late = db(rms(note, 17.0, 21.0));
        if (std::abs(early - late) > 4.0)
            std::printf("  voice %d: %.1f dB at 3 s, %.1f dB at 17 s\n", voice, early, late);
        assert(std::abs(early - late) <= 4.0);
    }
}

void testNotesEndOnRelease()
{
    Rig rig;
    rig.engine.noteOn(55, 0.9f);
    rig.render(1.5);
    rig.engine.noteOff(55);
    const auto tail = rig.render(4.0);
    assert(rms(tail, 3.0, 4.0) < 0.001);
}

void testChangingVoiceMidNoteStaysFiniteAndSounding()
{
    Rig rig;
    rig.engine.noteOn(64, 0.9f);
    rig.render(1.0);
    std::vector<float> all;
    for (int voice = 1; voice <= 2; ++voice)
    {
        rig.engine.setControl("sel", "select", static_cast<float>(voice));
        const auto part = rig.render(0.5);
        all.insert(all.end(), part.begin(), part.end());
    }
    assert(rms(all, 0.0, 1.0) > 0.01);
    for (std::size_t i = 1; i < all.size(); ++i)
        assert(std::abs(all[i] - all[i - 1]) < 0.4f);       // no step at the changeover
}

}  // namespace

int main()
{
    testSilentUntilPlayedAndEveryVoiceSounds();
    testEveryVoiceIsInTuneAcrossTheKeyboard();
    testTheVoicesAreAboutAsLoudAsEachOther();
    testANoteOutlastsItsRecording();
    testNotesEndOnRelease();
    testChangingVoiceMidNoteStaysFiniteAndSounding();
    std::printf("SampledStringsTest: passed\n");
    return 0;
}

// tests/engine/MetalBashTest.cpp
//
// examples/metal-bash.ttl: a hammer (noise through a granulator) striking two
// parallel modal banks, then distortion. The properties that make it a struck
// sheet and not just a sound: it is silent until hit, its lowest partial is the
// MIDI note, the plate's inharmonic partials are there, a harder hit is louder
// and brighter, and the ring dies away.

#include "ExampleRig.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

using valis::test::ExampleRig;

namespace {

constexpr double kRate = ExampleRig::kRate;
constexpr double kPi = 3.14159265358979323846;

double amplitudeAt(const std::vector<float>& s, std::size_t from, std::size_t length, double hz)
{
    assert(from + length <= s.size());
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < length; ++i)
    {
        const double phase = 2.0 * kPi * hz * static_cast<double>(i) / kRate;
        re += s[from + i] * std::cos(phase);
        im += s[from + i] * std::sin(phase);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(length);
}

float peak(const std::vector<float>& s, std::size_t from, std::size_t to)
{
    float p = 0.0f;
    for (std::size_t i = from; i < std::min(to, s.size()); ++i)
    {
        assert(std::isfinite(s[i]));
        p = std::max(p, std::abs(s[i]));
    }
    return p;
}

/// Share of the energy above 1 kHz in a window.
double brightness(const std::vector<float>& s, std::size_t from, std::size_t length)
{
    double high = 0.0, all = 0.0;
    for (double hz = 100.0; hz < 8000.0; hz += 50.0)
    {
        const double a = amplitudeAt(s, from, length, hz);
        all += a;
        if (hz > 1000.0)
            high += a;
    }
    return high / all;
}

void testSilentUntilStruck()
{
    ExampleRig rig("metal-bash.ttl");
    assert(peak(rig.render(0.3), 0, 100000) == 0.0f);
}

void testTunedToTheNote()
{
    // C4 and C5: the plate's lowest mode follows the note, with the plate's
    // second mode at the square root of two above it.
    for (const int note : {60, 72})
    {
        const double f = 440.0 * std::pow(2.0, (note - 69) / 12.0);

        ExampleRig rig(std::string("metal-bash.ttl"));
        rig.engine.noteOn(note, 0.9f);
        const auto out = rig.render(1.0);

        const std::size_t from = 4800, length = 24000;
        const double fundamental = amplitudeAt(out, from, length, f);
        assert(fundamental > 0.003);
        assert(amplitudeAt(out, from, length, f * 1.41421356) > 0.003);   // the plate's second mode
        assert(fundamental > 6.0 * amplitudeAt(out, from, length, f * 1.19));   // and nothing between

        // Not the neighbouring semitone: the pitch is the note.
        assert(fundamental > 3.0 * amplitudeAt(out, from, length, f * 1.0595));
    }
}

void testHarderIsLouderAndBrighter()
{
    const auto strike = [](float velocity)
    {
        ExampleRig rig("metal-bash.ttl");
        rig.engine.noteOn(60, velocity);
        return rig.render(0.6);
    };

    const auto soft = strike(0.2f), hard = strike(1.0f);
    assert(peak(hard, 0, 30000) > 2.0f * peak(soft, 0, 30000));
    assert(brightness(hard, 100, 2400) > brightness(soft, 100, 2400));
}

void testTheRingDiesAway()
{
    ExampleRig rig("metal-bash.ttl");
    rig.engine.noteOn(60, 1.0f);
    auto out = rig.render(0.3);
    rig.engine.noteOff(60);
    const auto after = rig.render(8.7);
    out.insert(out.end(), after.begin(), after.end());

    assert(peak(out, 0, 24000) > 0.05f);                                  // struck
    assert(peak(out, out.size() - 24000, out.size()) < 0.002f);           // gone
    assert(peak(out, 0, out.size()) < 1.0f);                              // and never clipped
}

void testItIsPolyphonicAndTheKnobsReachEveryVoice()
{
    ExampleRig rig("metal-bash.ttl");
    assert(rig.model.params().size() >= 10);

    ExampleRig plain("metal-bash.ttl"), muted("metal-bash.ttl");
    muted.setParam("shimmer", -40.0f);
    for (auto* r : {&plain, &muted})
    {
        r->engine.noteOn(60, 1.0f);
        r->engine.noteOn(67, 1.0f);
    }
    const auto a = plain.render(0.5), b = muted.render(0.5);
    assert(a != b);

    // Two strikes together carry the second note's fundamental as well.
    const double g = 440.0 * std::pow(2.0, (67 - 69) / 12.0);
    assert(amplitudeAt(a, 4800, 18000, g) > 0.002);
}

}  // namespace

int main()
{
    testSilentUntilStruck();
    testTunedToTheNote();
    testHarderIsLouderAndBrighter();
    testTheRingDiesAway();
    testItIsPolyphonicAndTheKnobsReachEveryVoice();
    std::puts("MetalBashTest passed");
    return 0;
}

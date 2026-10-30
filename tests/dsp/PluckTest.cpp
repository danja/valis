// tests/dsp/PluckTest.cpp
//
// val:Pluck. The exciter for a plucked string: silent until struck, louder and brighter for a harder stroke, brighter
// for a harder plectrum, and shaped by where along the string it lands.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 4096;

std::vector<float> pluck(ElementTestFixture& rig, float velocity)
{
    const std::vector<float> silence(static_cast<std::size_t>(kBlock), 0.0f);
    rig.setNote(60, velocity, true);
    return rig.run(silence);
}

double centroid(const std::vector<float>& signal)
{
    const auto bins = ElementTestFixture::computeSpectrum(signal, 12);
    double weighted = 0.0, total = 0.0;
    for (std::size_t b = 1; b < bins.size() && b * kRate / kBlock < 8000.0; ++b)
    {
        weighted += bins[b] * b * kRate / kBlock;
        total += bins[b];
    }
    return total > 0.0 ? weighted / total : 0.0;
}

/// Energy of the signal within half a band of the centre, by a plain DFT. The burst sits at the start of the block,
/// where the window of ElementTestFixture::computeSpectrum would all but remove it.
double bandEnergy(const std::vector<float>& signal, double centre, double half)
{
    double sum = 0.0;
    for (double f = centre - half; f <= centre + half; f += 10.0)
    {
        double re = 0.0, im = 0.0;
        for (std::size_t n = 0; n < signal.size(); ++n)
        {
            const double phase = 6.283185307179586 * f * static_cast<double>(n) / kRate;
            re += signal[n] * std::cos(phase);
            im -= signal[n] * std::sin(phase);
        }
        sum += re * re + im * im;
    }
    return sum;
}

void testSilentUntilPlucked()
{
    ElementTestFixture rig("Pluck", kRate);
    const std::vector<float> silence(static_cast<std::size_t>(kBlock), 0.0f);
    assert(ElementTestFixture::measurePeak(rig.run(silence)) == 0.0f);
}

/// A pluck is over in a few milliseconds: it is an excitation, and the string makes the note.
void testBurstIsShort()
{
    ElementTestFixture rig("Pluck", kRate);
    rig.set("frequency", 220.0f);
    const auto out = pluck(rig, 1.0f);
    assert(ElementTestFixture::measurePeak(out) > 0.05f);
    float tail = 0.0f;
    for (std::size_t i = 1500; i < out.size(); ++i)
        tail = std::max(tail, std::abs(out[i]));
    std::printf("  pluck: peak %.3f, after 31 ms %.6f\n", ElementTestFixture::measurePeak(out), tail);
    assert(tail < 0.002f);
}

void testHarderStrokeIsLouderAndBrighter()
{
    ElementTestFixture soft("Pluck", kRate), hard("Pluck", kRate);
    const auto a = pluck(soft, 0.3f);
    const auto b = pluck(hard, 1.0f);
    std::printf("  pluck: peak %.3f soft, %.3f hard; centroid %.0f Hz soft, %.0f Hz hard\n",
                ElementTestFixture::measurePeak(a), ElementTestFixture::measurePeak(b), centroid(a), centroid(b));
    assert(ElementTestFixture::measurePeak(b) > ElementTestFixture::measurePeak(a) * 2.0f);
    assert(centroid(b) > centroid(a) * 1.1);
}

void testHardPlectrumIsBrighter()
{
    ElementTestFixture finger("Pluck", kRate), pick("Pluck", kRate);
    finger.set("hardness", 0.05f);
    pick.set("hardness", 0.95f);
    const double a = centroid(pluck(finger, 1.0f));
    const double b = centroid(pluck(pick, 1.0f));
    std::printf("  pluck: centroid %.0f Hz with a fingertip, %.0f Hz with a pick\n", a, b);
    assert(b > a * 1.6);
}

/// Plucked a quarter of the way along a string at 220 Hz, every fourth harmonic (880, 1760, 2640 Hz) has a node under
/// the finger and is missing, while the harmonics halfway between them (440, 1320, 2200 Hz) are at their strongest. One
/// burst of noise has a random spectrum, so the energies are summed over several plucks and several harmonics.
void testPositionRemovesHarmonics()
{
    ElementTestFixture rig("Pluck", kRate);
    rig.set("frequency", 220.0f);
    rig.set("position", 0.25f);
    rig.set("hardness", 0.9f);
    double missing = 0.0, present = 0.0;
    for (int k = 0; k < 12; ++k)
    {
        const auto out = pluck(rig, 1.0f);
        rig.setNote(60, 1.0f, false);                       // let the gate fall, so the next pluck is a new edge
        rig.run(std::vector<float>(static_cast<std::size_t>(kBlock), 0.0f));
        for (const double f : {880.0, 1760.0, 2640.0})
            missing += bandEnergy(out, f, 70.0);
        for (const double f : {440.0, 1320.0, 2200.0})
            present += bandEnergy(out, f, 70.0);
    }
    std::printf("  pluck: energy at the nodes %.2e, halfway between %.2e\n", missing, present);
    assert(missing < present * 0.3);
}

}  // namespace

int main()
{
    testSilentUntilPlucked();
    testBurstIsShort();
    testHarderStrokeIsLouderAndBrighter();
    testHardPlectrumIsBrighter();
    testPositionRemovesHarmonics();
    std::printf("PluckTest PASSED\n");
    return 0;
}

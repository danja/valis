// tests/engine/InfernoChoirTest.cpp
//
// circuits/inferno-choir.ttl (scripts/inferno/inferno.py), played through the engine as the plugin would play it:
// silent until a note, in range, in tune once the expression is taken out, quiet after release, and each of the four
// MIDI controllers doing what the header says (CC1 wails, CC2 screams, CC3 opens the vowel, CC4 lights the fire).

#include "ExampleRig.h"

#include "valis/ElementTestFixture.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using valis::test::ExampleRig;

namespace {

constexpr double kRate = ExampleRig::kRate;
const char* kFile = "inferno-choir.ttl";

/// Every expressive control out of the way, so a note is a steady voice.
void plain(ExampleRig& rig)
{
    for (const char* p : {"cry", "jitter", "shimmer", "mouth", "wail", "scream", "vowel", "inferno", "vel_scream", "waver", "roughness", "low_voice"})
        rig.setParam(p, p == std::string("low_voice") ? -60.0f : 0.0f);
    rig.setParam("onset_swoop", 1.0f);
    rig.setParam("release_dive", 1.0f);
    rig.setParam("choir_detune", 1.0f);
    rig.setParam("choir_width", 0.0f);
    rig.setParam("hall", 0.0f);
    rig.setParam("swell", 5.0f);
}

/// One played note, the tail after settling. `controllers` are set before the note.
std::vector<float> play(int note, double seconds, std::vector<std::pair<int, float>> controllers = {},
                        bool smooth = true)
{
    ExampleRig rig(kFile);
    if (smooth)
        plain(rig);
    for (const auto& [number, value] : controllers)
        rig.engine.setController(number, value);
    rig.engine.noteOn(note, 1.0f);
    auto out = rig.render(seconds);
    for (const float v : out)
        assert(std::isfinite(v));
    return out;
}

std::vector<float> from(const std::vector<float>& v, double seconds)
{
    return std::vector<float>(v.begin() + static_cast<std::ptrdiff_t>(seconds * kRate), v.end());
}

double rmsDb(const std::vector<float>& v)
{
    double sum = 0.0;
    for (const float s : v) sum += static_cast<double>(s) * s;
    return 10.0 * std::log10(sum / static_cast<double>(std::max<std::size_t>(1, v.size())) + 1e-20);
}

/// The pitch within 15% of `expected`, the strongest lag there. A throat with a strong formant rings at a multiple of the
/// pitch, and a vibrato lowers the correlation at the true period below that of the ring, so a search for the strongest
/// lag anywhere reads the formant; confined to the expected period it reads the voice.
double pitchNear(const std::vector<float>& v, std::size_t at, double expected, std::size_t window = 4096)
{
    const auto lo = static_cast<std::size_t>(kRate / (expected * 1.15)), hi = static_cast<std::size_t>(kRate / (expected * 0.85));
    std::vector<double> ac(hi + 2, 0.0);
    std::size_t best = lo;
    for (std::size_t k = lo - 1; k <= hi + 1; ++k)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i + k < window; ++i)
            sum += static_cast<double>(v[at + i]) * v[at + i + k];
        ac[k] = sum;
        if (k >= lo && k <= hi && sum > ac[best]) best = k;
    }
    const double d = ac[best - 1] - 2.0 * ac[best] + ac[best + 1];
    return kRate / (static_cast<double>(best) + (d != 0.0 ? 0.5 * (ac[best - 1] - ac[best + 1]) / d : 0.0));
}

/// How far the pitch wanders over the render: the spread between the 10th and 90th percentile of the pitch, as a
/// fraction of the median, so one window that finds a harmonic does not decide it.
double swing(const std::vector<float>& v)   // of a note at 220 Hz
{
    std::vector<double> pitches;
    for (std::size_t at = 0; at + 4096 + 2048 < v.size(); at += 1200)
        pitches.push_back(pitchNear(v, at, 220.0));
    if (std::getenv("INFERNO_DEBUG")) { for (double f : pitches) std::printf("%.0f ", f); std::printf("\n"); }
    std::sort(pitches.begin(), pitches.end());
    const auto n = pitches.size();
    return (pitches[n * 9 / 10] - pitches[n / 10]) / pitches[n / 2];
}

/// The share of the signal's energy above `hz`, from one spectrum.
double centroid(const std::vector<float>& v)
{
    const auto tail = std::vector<float>(v.end() - 16384, v.end());
    const auto bins = valis::ElementTestFixture::computeSpectrum(tail, 14);
    double num = 0.0, den = 0.0;
    for (std::size_t b = 1; b < bins.size(); ++b)
    {
        const double f = static_cast<double>(b) * kRate / 16384.0, e = static_cast<double>(bins[b]) * bins[b];
        num += f * e; den += e;
    }
    return num / den;
}

/// Energy between two frequencies, from one spectrum.
double between(const std::vector<float>& v, double from, double to)
{
    const auto tail = std::vector<float>(v.end() - 16384, v.end());
    const auto bins = valis::ElementTestFixture::computeSpectrum(tail, 14);
    double sum = 0.0;
    for (std::size_t b = 1; b < bins.size(); ++b)
    {
        const double f = static_cast<double>(b) * kRate / 16384.0;
        if (f >= from && f < to) sum += static_cast<double>(bins[b]) * bins[b];
    }
    return sum;
}

/// The fraction of the energy above `hz`.
double above(const std::vector<float>& v, double hz)
{
    const auto tail = std::vector<float>(v.end() - 16384, v.end());
    const auto bins = valis::ElementTestFixture::computeSpectrum(tail, 14);
    double high = 0.0, all = 0.0;
    for (std::size_t b = 1; b < bins.size(); ++b)
    {
        const double e = static_cast<double>(bins[b]) * bins[b];
        all += e;
        if (static_cast<double>(b) * kRate / 16384.0 > hz) high += e;
    }
    return high / all;
}

}  // namespace

int main()
{
    // silent until played
    {
        ExampleRig rig(kFile);
        const auto out = rig.render(0.5);
        assert(rmsDb(out) < -100.0);
    }

    // played at its defaults: audible, in range
    {
        const auto out = play(57, 2.0, {}, false);
        const double level = rmsDb(from(out, 0.5));
        const float peak = *std::max_element(out.begin(), out.end());
        std::printf("  defaults: rms %.1f dB, peak %.2f\n", level, static_cast<double>(peak));
        assert(level > -30.0 && level < -3.0);
        assert(peak > 0.05f && peak <= 1.0f);
    }

    // in tune, with the expression taken out
    for (const int note : {48, 57, 69, 81})
    {
        const double want = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const auto out = from(play(note, 1.6), 0.6);
        const double got = pitchNear(out, 0, want);
        std::printf("  note %d: %.1f Hz (wanted %.1f)\n", note, got, want);
        assert(std::abs(got / want - 1.0) < 0.015);
    }

    // silent after release (a short release and no hall so the tail is brief)
    {
        ExampleRig rig(kFile);
        plain(rig);
        rig.setParam("release", 40.0f);
        rig.engine.noteOn(57, 1.0f);
        rig.render(0.6);
        rig.engine.noteOff(57);
        rig.render(1.0);
        assert(rmsDb(rig.render(0.5)) < -90.0);
    }

    // CC1 wails: the pitch wanders, and it does not without the controller
    {
        const double still = swing(from(play(57, 2.0), 0.5));
        const double wail = swing(from(play(57, 2.0, {{1, 1.0f}}), 0.5));
        std::printf("  CC1: swing %.3f without, %.3f with\n", still, wail);
        assert(still < 0.02 && wail > 0.04 && wail < 0.25);
    }

    // CC2 screams: the pitch lifts, the tone brightens
    {
        const auto calm = from(play(57, 2.0), 0.6), loud = from(play(57, 2.0, {{2, 1.0f}}), 0.6);
        const double lift = pitchNear(loud, 0, 260.0) / pitchNear(calm, 0, 220.0);
        std::printf("  CC2: pitch x%.3f, energy above 4 kHz %.4f to %.4f\n", lift, above(calm, 4000), above(loud, 4000));
        assert(lift > 1.12 && lift < 1.24);
        assert(above(loud, 4000) > above(calm, 4000) * 1.3);
    }

    // CC3 opens the vowel: the first formant moves up from about 320 Hz towards 900 Hz, so energy moves from the
    // bottom octave to the top of the first formant's range
    {
        const auto shut = from(play(57, 2.0), 0.6), open = from(play(57, 2.0, {{3, 1.0f}}), 0.6);
        const double before = between(shut, 600.0, 1300.0) / between(shut, 200.0, 500.0);
        const double after = between(open, 600.0, 1300.0) / between(open, 200.0, 500.0);
        std::printf("  CC3: energy 600-1300 Hz over 200-500 Hz, %.2f to %.2f\n", before, after);
        assert(after > before * 2.0);
    }

    // CC4 lights the fire: far more high-frequency energy, and still finite and within full scale
    {
        const auto cool = from(play(57, 2.0), 0.6), fire = from(play(57, 2.0, {{4, 1.0f}}), 0.6);
        std::printf("  CC4: centroid %.0f to %.0f Hz\n", centroid(cool), centroid(fire));
        assert(centroid(fire) > centroid(cool) * 1.5);
        assert(*std::max_element(fire.begin(), fire.end()) <= 1.0f);
    }

    // a controller that is not sent leaves the knobs in charge; a different controller changes nothing
    {
        const auto base = from(play(57, 1.5), 0.5), other = from(play(57, 1.5, {{5, 1.0f}, {7, 1.0f}}), 0.5);
        assert(std::abs(rmsDb(base) - rmsDb(other)) < 0.01);
    }

    std::puts("InfernoChoir ok");
    return 0;
}

// tests/dsp/GlottalTest.cpp
//
// val:Glottal. Every shape must sit at the pitch it was asked for, carry no DC, stay in range and be the same every
// render; the voice qualities must move the spectrum the way a voice does (a pressed pulse is brighter than a breathy one,
// a larger Rd or a lower open quotient is brighter or darker as documented, tilt darkens); jitter varies the cycles
// without moving the pitch; and the LF pulse has its sharp negative peak and a zero mean.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> render(ElementTestFixture& rig, int samples)
{
    std::vector<float> all;
    while (static_cast<int>(all.size()) < samples)
    {
        const auto part = rig.run(std::vector<float>(512, 0.0f));
        all.insert(all.end(), part.begin(), part.end());
    }
    return all;
}

std::vector<float> make(int shape, double hz, float open = 0.6f, float rd = 1.0f, int samples = 24000,
                        float tilt = 0.0f, float jitter = 0.0f)
{
    ElementTestFixture rig("Glottal", kRate);
    rig.set("frequency", static_cast<float>(hz));
    rig.set("shape", static_cast<float>(shape));
    rig.set("open", open);
    rig.set("rd", rd);
    rig.set("tilt", tilt);
    rig.set("jitter", jitter);
    return render(rig, samples);
}

/// The pitch of the last 8192 samples, from the first autocorrelation peak within 15% of the best, refined between samples.
double pitchOf(const std::vector<float>& v, double expected)
{
    const std::size_t at = v.size() - 8192, window = 8192;
    const auto lo = static_cast<std::size_t>(kRate / (expected * 1.6)), hi = static_cast<std::size_t>(kRate / (expected * 0.6));
    std::vector<double> ac(hi + 2, 0.0);
    double best = 0.0;
    for (std::size_t k = lo - 1; k <= hi + 1; ++k)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i + k < window; ++i)
            sum += static_cast<double>(v[at + i]) * v[at + i + k];
        ac[k] = sum;
        if (k >= lo && k <= hi) best = std::max(best, sum);
    }
    for (std::size_t k = lo; k <= hi; ++k)
        if (ac[k] >= 0.85 * best && ac[k] >= ac[k - 1] && ac[k] >= ac[k + 1])
        {
            const double d = ac[k - 1] - 2.0 * ac[k] + ac[k + 1];
            return kRate / (static_cast<double>(k) + (d != 0.0 ? 0.5 * (ac[k - 1] - ac[k + 1]) / d : 0.0));
        }
    return 0.0;
}

double mean(const std::vector<float>& v)
{
    double s = 0.0;
    for (std::size_t i = v.size() / 2; i < v.size(); ++i) s += v[i];
    return s / static_cast<double>(v.size() - v.size() / 2);
}

/// Energy above `hz` as a fraction of the whole, from the last 16384 samples.
double above(const std::vector<float>& v, double hz)
{
    const std::vector<float> tail(v.end() - 16384, v.end());
    const auto bins = ElementTestFixture::computeSpectrum(tail, 14);
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
    const char* names[] = {"Rosenberg", "Klatt", "LF", "Radiated", "Cosine", "Saw", "Pulse"};

    // pitch, DC and range, for every shape at three pitches
    for (int shape = 0; shape < 7; ++shape)
        for (const double hz : {110.0, 220.0, 440.0})
        {
            const auto v = make(shape, hz);
            const double got = pitchOf(v, hz);
            const float peak = *std::max_element(v.begin(), v.end()), low = *std::min_element(v.begin(), v.end());
            std::printf("  %-9s %5.0f Hz: pitch %.2f, mean %+.4f, range %+.2f to %+.2f\n", names[shape], hz, got, mean(v),
                        static_cast<double>(low), static_cast<double>(peak));
            assert(std::abs(got / hz - 1.0) < 0.005);
            assert(std::abs(mean(v)) < 0.02);
            assert(peak < 2.0f && low > -2.0f && peak - low > 0.4f);
            for (const float s : v) assert(std::isfinite(s));
        }

    // the same every render
    assert(make(2, 220.0, 0.6f, 1.0f, 8192, 0.0f, 0.03f) == make(2, 220.0, 0.6f, 1.0f, 8192, 0.0f, 0.03f));

    // the LF pulse: a sharp negative peak at closure, and a smaller positive one
    {
        const auto v = make(2, 110.0, 0.6f, 0.5f);
        const float peak = *std::max_element(v.begin() + 12000, v.end()), low = *std::min_element(v.begin() + 12000, v.end());
        std::printf("  LF Rd 0.5: positive %+.2f, negative %+.2f\n", static_cast<double>(peak), static_cast<double>(low));
        assert(low < -0.7f && peak > 0.05f && peak < -low);
    }

    // a pressed pulse is brighter than a breathy one, in each of the ways to make it
    {
        const double tense = above(make(2, 110.0, 0.6f, 0.4f, 32768), 2000.0), breathy = above(make(2, 110.0, 0.6f, 2.6f, 32768), 2000.0);
        std::printf("  LF energy above 2 kHz: Rd 0.4 %.4f, Rd 2.6 %.4f\n", tense, breathy);
        assert(tense > breathy * 3.0);

        const double narrow = above(make(0, 110.0, 0.3f, 1.0f, 32768), 2000.0), wide = above(make(0, 110.0, 0.95f, 1.0f, 32768), 2000.0);
        std::printf("  Rosenberg above 2 kHz: open 0.3 %.5f, open 0.95 %.5f\n", narrow, wide);
        assert(narrow > wide * 2.0);

        const double crisp = above(make(2, 110.0, 0.6f, 1.0f, 32768, 0.0f), 2000.0), dull = above(make(2, 110.0, 0.6f, 1.0f, 32768, 0.8f), 2000.0);
        std::printf("  LF above 2 kHz: tilt 0 %.4f, tilt 0.8 %.5f\n", crisp, dull);
        assert(crisp > dull * 5.0);
    }

    // a glottal pulse is darker than a saw at the same pitch (the reason to use one)
    {
        const double saw = above(make(5, 110.0, 0.6f, 1.0f, 32768), 3000.0), flow = above(make(0, 110.0, 0.6f, 1.0f, 32768), 3000.0);
        std::printf("  above 3 kHz: saw %.4f, Rosenberg flow %.6f\n", saw, flow);
        assert(saw > flow * 20.0);
    }

    // jitter varies the cycles without moving the pitch
    {
        const auto still = make(2, 220.0, 0.6f, 1.0f, 48000), shaky = make(2, 220.0, 0.6f, 1.0f, 48000, 0.0f, 0.04f);
        assert(still != shaky);
        const double hz = pitchOf(shaky, 220.0);
        std::printf("  jitter 0.04: pitch %.1f Hz\n", hz);
        assert(std::abs(hz / 220.0 - 1.0) < 0.04);
    }

    // breath adds noise to a quiet stretch of the cycle: the closed phase is no longer exactly flat
    {
        ElementTestFixture rig("Glottal", kRate);
        rig.set("frequency", 110.0f); rig.set("shape", 0.0f); rig.set("breath", 0.5f);
        const auto breathy = render(rig, 24000), clean = make(0, 110.0);
        double diff = 0.0;
        for (std::size_t i = 12000; i < clean.size(); ++i) diff += std::abs(breathy[i] - clean[i]);
        assert(diff / 12000.0 > 0.01);
    }

    std::puts("Glottal ok");
    return 0;
}

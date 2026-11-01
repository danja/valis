// tests/dsp/ToneStackTest.cpp
//
// val:ToneStack. The element is the digital version of a published analog transfer function, so it is held to
// that function: the gain it applies to a sine at each frequency must match the paper's H(s) at the same settings.
// The shape is also checked against what the paper's Fig. 2 shows for every control at half: a peak near 40 Hz and
// a notch near 700 Hz about 12 dB down.

#include "valis/DspElement.h"
#include "valis/ElementTestFixture.h"
#include "../../src/dsp/elements/ToneStackCoefficients.h"

#include <cassert>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

/// Gain in decibels of the element for a sine at `hz`, from the last half of the render.
double measured(double rate, double hz, float treble, float middle, float bass)
{
    ElementTestFixture rig("ToneStack", rate);
    rig.set("treble", treble);
    rig.set("middle", middle);
    rig.set("bass", bass);
    const int n = static_cast<int>(rate * 3.0);
    const auto out = rig.run(ElementTestFixture::generateSine(hz, rate, n, 0.1f));

    double in2 = 0.0, out2 = 0.0;
    for (int i = n / 2; i < n; ++i)
    {
        const double x = 0.1 * std::sin(2.0 * M_PI * hz * i / rate);
        in2  += x * x;
        out2 += static_cast<double>(out[static_cast<std::size_t>(i)]) * out[static_cast<std::size_t>(i)];
    }
    return 10.0 * std::log10(out2 / in2);
}

double analogDb(double hz, double treble, double middle, double bass)
{
    const auto h = elements::tonestack::analog({}, treble, middle, elements::tonestack::bassTaper(bass));
    return 20.0 * std::log10(std::abs(h.response(hz)));
}

/// The circuit of the paper's Fig. 1 solved node by node, with no use of the closed form: the check that the
/// coefficients were copied correctly. Nodes: 0 top of the treble pot after C1, 1 the output wiper, 2 the bottom
/// of the treble pot, 3 the far side of R4, 4 the bass pot's lower end, 5 the middle pot's wiper.
std::complex<double> solved(double hz, double t, double m, double l)
{
    using Complex = std::complex<double>;
    const elements::tonestack::Components k;
    const Complex s(0.0, 2.0 * M_PI * hz);
    constexpr double tiny = 1.0e-9;
    std::array<std::array<Complex, 7>, 6> a {};   // the last column is the right-hand side

    const auto link = [&](int p, int q, Complex y)
    {
        a[p][p] += y;
        if (q >= 0) { a[q][q] += y; a[p][q] -= y; a[q][p] -= y; }
    };
    const auto fromInput = [&](int p, Complex y) { a[p][p] += y; a[p][6] += y; };

    fromInput(0, s * k.c1);
    link(0, 1, 1.0 / std::max((1.0 - t) * k.r1, tiny));
    link(1, 2, 1.0 / std::max(t * k.r1, tiny));
    fromInput(3, 1.0 / k.r4);
    link(3, 2, s * k.c2);
    link(2, 4, 1.0 / std::max(l * k.r2, tiny));
    link(4, 5, 1.0 / std::max((1.0 - m) * k.r3, tiny));
    a[5][5] += 1.0 / std::max(m * k.r3, tiny);
    link(3, 5, s * k.c3);

    for (int c = 0; c < 6; ++c)
    {
        int pivot = c;
        for (int r = c + 1; r < 6; ++r)
            if (std::abs(a[r][c]) > std::abs(a[pivot][c])) pivot = r;
        std::swap(a[c], a[pivot]);
        for (int r = c + 1; r < 6; ++r)
        {
            const Complex f = a[r][c] / a[c][c];
            for (int q = c; q < 7; ++q) a[r][q] -= f * a[c][q];
        }
    }
    std::array<Complex, 6> v {};
    for (int r = 5; r >= 0; --r)
    {
        Complex sum = a[r][6];
        for (int q = r + 1; q < 6; ++q) sum -= a[r][q] * v[q];
        v[r] = sum / a[r][r];
    }
    return v[1];
}

void testTheClosedFormIsTheCircuit()
{
    for (const double t : {0.1, 0.5, 0.9})
        for (const double m : {0.2, 0.5, 0.9})
            for (const double l : {0.05, 0.5, 0.95})
                for (const double hz : {20.0, 150.0, 700.0, 4000.0})
                {
                    const auto closed = elements::tonestack::analog({}, t, m, l).response(hz);
                    const auto nodal  = solved(hz, t, m, l);
                    assert(std::abs(closed - nodal) < 1.0e-9 * std::abs(nodal));
                }
}

void testMatchesThePapersTransferFunction()
{
    struct Setting { float t, m, l; };
    const Setting settings[] = {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.2f}, {0.0f, 1.0f, 1.0f}, {0.8f, 0.3f, 0.7f}};
    for (const double rate : {44100.0, 48000.0, 96000.0})
        for (const auto& s : settings)
            for (const double hz : {30.0, 100.0, 400.0, 700.0, 1500.0, 3000.0, 6000.0})
            {
                const double got  = measured(rate, hz, s.t, s.m, s.l);
                const double want = analogDb(hz, s.t, s.m, s.l);
                // The bilinear transform bends the top octave, and more at a low rate.
                const double allowed = hz > 3000.0 ? 1.0 : 0.3;
                if (std::abs(got - want) > allowed)
                    std::printf("rate %.0f t %.1f m %.1f l %.1f at %.0f Hz: %.2f dB, wanted %.2f\n",
                                rate, s.t, s.m, s.l, hz, got, want);
                assert(std::abs(got - want) <= allowed);
            }
}

void testFigureTwoShape()
{
    // Fig. 2 has the bass pot at half its resistance; with the logarithmic taper that is a knob at 0.796.
    const float bass = static_cast<float>(1.0 + std::log(0.5) / 3.4);
    const double peak  = measured(48000.0, 40.0, 0.5f, 0.5f, bass);
    const double notch = measured(48000.0, 700.0, 0.5f, 0.5f, bass);
    const double top   = measured(48000.0, 10000.0, 0.5f, 0.5f, bass);
    std::printf("peak %.1f dB at 40 Hz, notch %.1f dB at 700 Hz, %.1f dB at 10 kHz\n", peak, notch, top);
    assert(peak > -3.0 && peak < 0.0);
    assert(notch < -11.0 && notch > -14.0);
    assert(top > -6.0 && top < -3.0);
    assert(notch < peak - 9.0 && notch < top - 6.0);
}

void testControlsInteract()
{
    // The middle control also moves the treble: at the same treble setting, full middle is louder at 3 kHz
    // than no middle, which an independent three-band equaliser would not do.
    const double without = measured(48000.0, 3000.0, 0.5f, 0.0f, 0.5f);
    const double with    = measured(48000.0, 3000.0, 0.5f, 1.0f, 0.5f);
    std::printf("3 kHz: middle 0 %.1f dB, middle 1 %.1f dB\n", without, with);
    assert(std::abs(with - without) > 1.0);
}

void testStableAtAnyRateAndSetting()
{
    for (const double rate : {44100.0, 192000.0})
        for (const float t : {0.0f, 1.0f})
            for (const float m : {0.0f, 1.0f})
                for (const float l : {0.0f, 1.0f})
                {
                    ElementTestFixture rig("ToneStack", rate);
                    rig.set("treble", t); rig.set("middle", m); rig.set("bass", l);
                    const auto out = rig.run(ElementTestFixture::generateSine(200.0, rate, static_cast<int>(rate), 0.5f));
                    for (const float v : out)
                        assert(std::isfinite(v) && std::abs(v) < 2.0f);
                }
}

}  // namespace

int main()
{
    testTheClosedFormIsTheCircuit();
    testMatchesThePapersTransferFunction();
    testFigureTwoShape();
    testControlsInteract();
    testStableAtAnyRateAndSetting();
    std::puts("ToneStackTest passed");
    return 0;
}

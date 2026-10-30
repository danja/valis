// tests/shot/SpectralDistanceTest.cpp
//
// valis::spectralDistance: zero for the same sound, the level difference for the same sound louder, and large
// for a different sound.

#include "valis/SpectralDistance.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

std::vector<float> tone(double hz, float amplitude)
{
    std::vector<float> out(48000);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = amplitude * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
    return out;
}

}  // namespace

int main()
{
    const auto a = tone(440.0, 0.3f);
    assert(spectralDistance(a, a, kRate).meanDb < 1.0e-6);

    const auto louder = tone(440.0, 0.6f);                      // 6 dB
    const auto d = spectralDistance(a, louder, kRate);
    std::printf("  distance: 6 dB louder reads %.2f dB\n", d.meanDb);
    assert(d.meanDb > 5.0 && d.meanDb < 7.0);
    assert(d.perResolution.size() == 3);

    const auto other = spectralDistance(a, tone(880.0, 0.3f), kRate);
    std::printf("  distance: an octave higher reads %.2f dB\n", other.meanDb);
    assert(other.meanDb > 15.0);

    assert(spectralDistance(a, tone(450.0, 0.3f), kRate).meanDb < other.meanDb);   // a near sound is nearer
    std::printf("SpectralDistanceTest PASSED\n");
    return 0;
}

// src/dsp/elements/ToneStackCoefficients.h
//
// The transfer function of the passive treble, middle and bass network of a Fender '59 Bassman, in closed
// form, from Yeh and Smith, "Discretization of the '59 Fender Bassman Tone Stack", DAFx-06
// (https://dafx.de/paper-archive/2006/papers/p_001.pdf), equation 1:
//
//   H(s) = (b1 s + b2 s^2 + b3 s^3) / (1 + a1 s + a2 s^2 + a3 s^3)
//
// t, m and l are the treble, middle and bass pots as a fraction of their travel. The network is a third-order
// passive circuit, so its poles do not depend on the treble control and the controls interact.

#pragma once

#include <cmath>
#include <complex>

namespace valis::elements::tonestack {

/// Component values of the Fender '59 Bassman stack (the paper's Fig. 1).
struct Components
{
    double c1 = 250.0e-12, c2 = 20.0e-9, c3 = 20.0e-9;       ///< farads
    double r1 = 250.0e3, r2 = 1.0e6, r3 = 25.0e3, r4 = 56.0e3; ///< ohms: treble, bass, middle, slope
};

struct Analog
{
    double b[4] = {0.0, 0.0, 0.0, 0.0};
    double a[4] = {1.0, 0.0, 0.0, 0.0};

    std::complex<double> response(double hz) const noexcept
    {
        const std::complex<double> s(0.0, 6.283185307179586 * hz);
        const std::complex<double> num = s * (b[1] + s * (b[2] + s * b[3]));
        const std::complex<double> den = a[0] + s * (a[1] + s * (a[2] + s * a[3]));
        return num / den;
    }
};

/// The bass pot is logarithmic: a turn of 0 to 1 sweeps its resistance as exp(3.4 (bass - 1)), the taper the
/// open-source Guitarix model uses for the same circuit.
inline double bassTaper(double bass) noexcept { return std::exp(3.4 * (bass - 1.0)); }

inline Analog analog(const Components& k, double t, double m, double l) noexcept
{
    const double C1 = k.c1, C2 = k.c2, C3 = k.c3, R1 = k.r1, R2 = k.r2, R3 = k.r3, R4 = k.r4;
    const double K = C1 * C2 * C3;
    Analog h;
    h.b[1] = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
    h.b[2] = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4)
           - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
           + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
           + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
    h.b[3] = l * m * (K * R1 * R2 * R3 + K * R2 * R3 * R4)
           - m * m * (K * R1 * R3 * R3 + K * R3 * R3 * R4)
           + m * (K * R1 * R3 * R3 + K * R3 * R3 * R4)
           + t * K * R1 * R3 * R4 - t * m * K * R1 * R3 * R4 + t * l * K * R1 * R2 * R4;
    h.a[1] = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
    h.a[2] = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
           - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
           + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3
              + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
    h.a[3] = l * m * (K * R1 * R2 * R3 + K * R2 * R3 * R4)
           - m * m * (K * R1 * R3 * R3 + K * R3 * R3 * R4)
           + m * (K * R3 * R3 * R4 + K * R1 * R3 * R3 - K * R1 * R3 * R4)
           + l * K * R1 * R2 * R4 + K * R1 * R3 * R4;
    return h;
}

}  // namespace valis::elements::tonestack

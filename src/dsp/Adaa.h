// src/dsp/Adaa.h
//
// Antiderivative anti-aliasing for memoryless nonlinearities, after Bilbao,
// Esqueda, Parker and Välimäki, in the formulation Jatin Chowdhury popularised.
//
// A waveshaper generates harmonics above Nyquist that fold back as aliasing.
// Oversampling pushes the fold-back up; ADAA instead integrates the transfer
// function across the interval between successive samples, which suppresses the
// aliasing without running the whole graph at a higher rate.

#pragma once

#include <cmath>

namespace valis::dsp {

inline constexpr double kPi = 3.14159265358979323846;

/// Dilogarithm for z in [-1, 0], via Landen's identity
///     Li2(z) = -log^2(1-z)/2 - Li2(z/(z-1))
/// which maps that range to w in [0, 0.5], where the defining series
/// Li2(w) = sum w^k / k^2 converges quickly.
inline double dilogUnitNegative(double z) noexcept
{
    const double w = z / (z - 1.0);      // in [0, 0.5]

    double term = w;
    double sum  = w;
    for (int k = 2; k < 40; ++k)
    {
        term *= w;
        const double contribution = term / (static_cast<double>(k) * k);
        sum += contribution;
        if (contribution < 1e-18)
            break;
    }

    const double l = std::log1p(-z);
    return -0.5 * l * l - sum;
}

/// Dilogarithm for all z <= 0. ADAA2 tanh evaluates Li2(-exp(-2x)), whose
/// argument runs to large negative values as x goes negative, so the [-1, 0]
/// branch alone is not enough: below -1 use the inversion formula
///     Li2(z) = -pi^2/6 - log^2(-z)/2 - Li2(1/z)
/// which brings the argument back into (-1, 0).
inline double dilogNegative(double z) noexcept
{
    if (z >= 0.0)
        return 0.0;

    if (z < -1.0)
    {
        const double l = std::log(-z);
        return -kPi * kPi / 6.0 - 0.5 * l * l - dilogUnitNegative(1.0 / z);
    }

    return dilogUnitNegative(z);
}

/// First-order ADAA. Needs only the first antiderivative, which is elementary
/// for every transfer function Valis ships.
template <typename Shape>
class Adaa1
{
public:
    void reset() noexcept { x1 = 0.0; ad1x1 = Shape::antiderivative1(0.0); }

    double process(double x0) noexcept
    {
        const double ad1x0 = Shape::antiderivative1(x0);
        const double diff  = x0 - x1;

        double y;
        if (std::abs(diff) < tolerance)
            y = Shape::evaluate(0.5 * (x0 + x1));   // limit as the interval closes
        else
            y = (ad1x0 - ad1x1) / diff;

        x1    = x0;
        ad1x1 = ad1x0;
        return y;
    }

private:
    static constexpr double tolerance = 1.0e-5;
    double x1    = 0.0;
    double ad1x1 = 0.0;
};

/// Second-order ADAA. Cleaner than Adaa1 under heavy drive, at the cost of a
/// second antiderivative and a sample more of delay.
///
/// Must be evaluated in double: tanh's second antiderivative overflows in float.
template <typename Shape>
class Adaa2
{
public:
    void reset() noexcept
    {
        x1 = x2 = 0.0;
        d1 = 0.0;
    }

    double process(double x0) noexcept
    {
        const double d0 = calcD(x0, x1);

        double y;
        const double diff = x0 - x2;
        if (std::abs(diff) < tolerance)
        {
            // Both intervals have closed: fall back to the midpoint value.
            const double xBar = 0.5 * (x0 + x2);
            const double delta = xBar - x1;

            if (std::abs(delta) < tolerance)
                y = Shape::evaluate(0.5 * (xBar + x1));
            else
                y = (2.0 / delta)
                  * (Shape::antiderivative1(xBar)
                     + (Shape::antiderivative2(x1) - Shape::antiderivative2(xBar)) / delta);
        }
        else
        {
            y = (2.0 / diff) * (d0 - d1);
        }

        d1 = d0;
        x2 = x1;
        x1 = x0;
        return y;
    }

private:
    static constexpr double tolerance = 1.0e-5;

    static double calcD(double x0, double x1) noexcept
    {
        const double diff = x0 - x1;
        if (std::abs(diff) < tolerance)
            return Shape::antiderivative1(0.5 * (x0 + x1));

        return (Shape::antiderivative2(x0) - Shape::antiderivative2(x1)) / diff;
    }

    double x1 = 0.0, x2 = 0.0, d1 = 0.0;
};

// ---------------------------------------------------------------------------
// Transfer functions. Each supplies evaluate() and as many antiderivatives as
// it can express in closed form.
// ---------------------------------------------------------------------------

struct TanhShape
{
    static double evaluate(double x) noexcept { return std::tanh(x); }

    static double antiderivative1(double x) noexcept
    {
        // log(cosh(x)) written to stay finite for large |x|.
        const double ax = std::abs(x);
        return ax + std::log1p(std::exp(-2.0 * ax)) - std::log(2.0);
    }

    static double antiderivative2(double x) noexcept
    {
        const double expVal = std::exp(-2.0 * x);
        return 0.5 * (dilogNegative(-expVal)
                      - x * (x + 2.0 * std::log1p(expVal) - 2.0 * antiderivative1(x)))
             + (kPi * kPi / 24.0);
    }
};

struct HardClipShape
{
    static double evaluate(double x) noexcept { return x < -1.0 ? -1.0 : (x > 1.0 ? 1.0 : x); }

    static double antiderivative1(double x) noexcept
    {
        return std::abs(x) <= 1.0 ? 0.5 * x * x : std::abs(x) - 0.5;
    }

    static double antiderivative2(double x) noexcept
    {
        if (std::abs(x) <= 1.0)
            return x * x * x / 6.0;

        const double ax = std::abs(x);
        const double v  = 1.0 / 6.0 + 0.5 * ax * ax - 0.5 * ax;
        return x < 0.0 ? -v : v;
    }
};

/// y = x / sqrt(x^2 + 1). From the Scream reference.
struct SinArcTanShape
{
    static double evaluate(double x) noexcept { return x / std::sqrt(x * x + 1.0); }
    static double antiderivative1(double x) noexcept { return std::sqrt(x * x + 1.0); }
    static double antiderivative2(double x) noexcept
    {
        const double r = std::sqrt(x * x + 1.0);
        return 0.5 * (x * r + std::asinh(x));
    }
};

/// y = x / (|x| + 1). Softer knee than SinArcTan.
struct SoftSineShape
{
    static double evaluate(double x) noexcept { return x / (std::abs(x) + 1.0); }

    static double antiderivative1(double x) noexcept
    {
        const double ax = std::abs(x);
        return ax - std::log1p(ax);
    }

    static double antiderivative2(double x) noexcept
    {
        const double ax = std::abs(x);
        const double v  = 0.5 * ax * ax + ax - (ax + 1.0) * std::log1p(ax);
        return x < 0.0 ? -v : v;
    }
};

}  // namespace valis::dsp

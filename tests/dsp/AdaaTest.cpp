// tests/dsp/AdaaTest.cpp
//
// Second-order ADAA for tanh evaluates exp(-2x), which overflows for x below about -354. The
// resulting NaN lodged in every filter downstream and silenced a circuit whose dry click ran into
// a drive stage (see MISTAKES.md). The input of a waveshaper has no bound, so the shape must stay
// finite wherever it is asked.

#include "dsp/Adaa.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace valis::dsp;

namespace {

void testFiniteForLargeInput()
{
    for (double x : {-1.0e6, -5000.0, -800.0, -354.0, -300.0, -50.0, 0.0, 50.0, 300.0, 800.0, 5000.0, 1.0e6})
    {
        assert(std::isfinite(TanhShape::antiderivative1(x)));
        assert(std::isfinite(TanhShape::antiderivative2(x)));
    }
}

/// The mirrored branch must join the direct one without a step, or a signal crossing -300 clicks.
void testContinuousAtTheSwitchOver()
{
    const double below = TanhShape::antiderivative2(-300.0 - 1.0e-6);
    const double above = TanhShape::antiderivative2(-300.0 + 1.0e-6);
    assert(std::abs(below - above) < 1.0e-2);   // the function itself is of order 4.5e4 here

    // It is the integral of log cosh, which is even and starts from zero, so it is odd: f(x) + f(-x) = 0.
    for (double x : {1.0, 10.0, 100.0, 299.0})
        assert(std::abs(TanhShape::antiderivative2(x) + TanhShape::antiderivative2(-x)) < 1.0e-6 * (1.0 + x * x));
}

/// A swing from far negative to far positive, as a loud click into a drive stage produces.
void testAdaa2SurvivesHugeSwings()
{
    Adaa2<TanhShape> shaper;
    shaper.reset();

    double peak = 0.0;
    const double levels[] = {-4000.0, 4000.0, -900.0, 900.0, 0.0, -400.0, 400.0, 0.5, -0.5};
    for (int round = 0; round < 50; ++round)
        for (double x : levels)
        {
            const double y = shaper.process(x);
            assert(std::isfinite(y));
            peak = std::max(peak, std::abs(y));
        }

    assert(peak <= 1.0 + 1.0e-6);   // tanh never leaves [-1, 1]
}

/// Ordinary input still follows tanh closely, so the guard has changed nothing in the usual range.
void testStillTanhInTheUsualRange()
{
    Adaa2<TanhShape> shaper;
    shaper.reset();
    double previous = 0.0, last = 0.0;
    for (int i = 0; i < 4000; ++i)
    {
        previous = last;
        last = shaper.process(2.0 * std::sin(i * 0.01));
    }
    const double expected = std::tanh(2.0 * std::sin(3999 * 0.01));
    assert(std::abs(last - expected) < 0.05);   // ADAA2 adds a sample of delay and some smoothing
    (void) previous;
}

} // namespace

int main()
{
    testFiniteForLargeInput();
    testContinuousAtTheSwitchOver();
    testAdaa2SurvivesHugeSwings();
    testStillTanhInTheUsualRange();
    std::puts("dsp_AdaaTest passed");
    return 0;
}

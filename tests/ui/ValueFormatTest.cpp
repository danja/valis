// tests/ui/ValueFormatTest.cpp

#include "valis/ValueFormat.h"

#include <cassert>
#include <cstdio>

using namespace valis;

namespace {

void testUnitChoosesPrecision()
{
    // A cutoff is read in whole hertz; the span heuristic alone would have given
    // this two decimals, because -60..12 dB and 20..12000 Hz say nothing about
    // how a reader wants each one written.
    assert(formatControlValue(440.0, "Hz", 20.0, 12000.0) == "440 Hz");
    assert(formatControlValue(1000.0, "Hz", 20.0, 12000.0) == "1000 Hz");

    // Below 100 Hz a decimal is worth having: tuning lives there.
    assert(formatControlValue(55.25, "Hz", 20.0, 200.0) == "55.2 Hz");

    // Level: one decimal, whatever the range.
    assert(formatControlValue(-6.0206, "dB", -60.0, 12.0) == "-6.0 dB");
    assert(formatControlValue(0.0, "dB", -60.0, 12.0) == "0.0 dB");

    assert(formatControlValue(150.0, "ms", 0.1, 200.0) == "150 ms");
    assert(formatControlValue(12.5, "ms", 0.1, 200.0) == "12.5 ms");
    assert(formatControlValue(1.5, "s", 0.0, 10.0) == "1.50 s");
    assert(formatControlValue(50.0, "%", 0.0, 100.0) == "50 %");
}

void testRangeIsTheFallbackWithoutAUnit()
{
    // No unit to go on, so the width of the range decides, as it did before.
    assert(formatControlValue(0.5, "", 0.0, 1.0) == "0.500");
    assert(formatControlValue(4.25, "", 0.0, 10.0) == "4.25");
    assert(formatControlValue(500.0, "", 0.0, 1000.0) == "500");
}

void testNegativeZeroIsPrintedAsZero()
{
    // Rounding a small negative value must not produce "-0.0", which reads as a
    // value the user set rather than as the artefact it is.
    assert(formatControlValue(-0.004, "dB", -60.0, 12.0) == "0.0 dB");
    assert(formatControlValue(-0.0001, "", 0.0, 1.0) == "0.000");

    // A real negative value keeps its sign. A -1..1 range spans 2, so the
    // fallback gives it two decimals.
    assert(formatControlValue(-0.5, "", -1.0, 1.0) == "-0.50");
}

void testUnknownUnitFallsBackToTheRange()
{
    // An unrecognised symbol must still be appended, and must not lose the
    // range heuristic on the way.
    assert(formatControlValue(2.5, "semitones", 0.0, 12.0) == "2.50 semitones");
}

}  // namespace

int main()
{
    testUnitChoosesPrecision();
    testRangeIsTheFallbackWithoutAUnit();
    testNegativeZeroIsPrintedAsZero();
    testUnknownUnitFallsBackToTheRange();

    std::puts("ValueFormatTest PASSED");
    return 0;
}

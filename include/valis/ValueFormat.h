// include/valis/ValueFormat.h
//
// How a control value is written out for display. The precision follows the
// port's declared unit rather than the width of its range, so a frequency reads
// as "440 Hz" and a gain as "-6.0 dB" instead of both being given whatever
// number of decimals their span happens to imply.
//
// Header-only and free of JUCE, so the rule is testable as plain data. Views
// decide layout; this decides only the text.

#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace valis {

/// Decimal places for a value in `unit`. `minimum` and `maximum` are the port's
/// declared range, used only when the unit says nothing.
inline int decimalsFor(double value, std::string_view unit, double minimum, double maximum)
{
    // Frequency: nobody reads a cutoff to two decimal places, but a sub-bass
    // tuning below 100 Hz is worth one.
    if (unit == "Hz")
        return std::abs(value) >= 100.0 ? 0 : 1;

    // Level: one decimal is the convention, and the ear cannot tell more.
    if (unit == "dB")
        return 1;

    // Time: milliseconds are read whole once they are past a few of them.
    if (unit == "ms")
        return std::abs(value) >= 100.0 ? 0 : 1;
    if (unit == "s")
        return 2;

    if (unit == "%")
        return 0;

    // No unit to go on, so fall back to the width of the range: a wide span is
    // read in whole numbers, a narrow one needs the decimals to move at all.
    const double span = maximum - minimum;
    if (span > 100.0) return 0;
    if (span > 1.0)   return 2;
    return 3;
}

/// The value as a view should print it, with the unit symbol appended when
/// there is one. A unit is separated by a space, as SI writing has it.
inline std::string formatControlValue(double value, std::string_view unit,
                                      double minimum, double maximum)
{
    const int decimals = decimalsFor(value, unit, minimum, maximum);

    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);

    std::string text(buffer);

    // "-0.0" is an artefact of rounding a small negative value, never something
    // the user set, so print it as zero.
    if (text.rfind("-0", 0) == 0 && text.find_first_of("123456789") == std::string::npos)
        text.erase(0, 1);

    if (! unit.empty())
    {
        text += ' ';
        text.append(unit);
    }

    return text;
}

}  // namespace valis

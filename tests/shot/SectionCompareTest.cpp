// tests/shot/SectionCompareTest.cpp
//
// valis::compareSections: which stretch of a track differs between two renders.

#include "valis/SectionCompare.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 24000.0;

void append(std::vector<float>& out, double hz, double seconds, float amplitude)
{
    const std::size_t start = out.size();
    const auto n = static_cast<std::size_t>(kRate * seconds);
    for (std::size_t i = 0; i < n; ++i)
        out.push_back(amplitude * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(start + i) / kRate)));
}

/// C for 20 s, G for 20 s, C for 20 s. `middleLevel` scales the middle part.
std::vector<float> track(float middleLevel = 1.0f, double thirdHz = 261.6256)
{
    std::vector<float> v;
    append(v, 261.6256, 20.0, 0.2f);
    append(v, 391.9954, 20.0, 0.4f * middleLevel);
    append(v, thirdHz, 20.0, 0.2f);
    return v;
}

bool near(double a, double b, double within) { return std::abs(a - b) <= within; }

void testIdenticalFilesDifferNowhere()
{
    const auto a = track();
    const auto c = compareSections(a, {}, a, {}, kRate);
    assert(c.sections.size() == 3);
    for (const auto& s : c.sections)
        assert(s.spectralDistanceDb < 0.01 && std::abs(s.levelDifferenceDb) < 0.01 && ! s.hasWidth);
}

void testTheChangedSectionIsNamed()
{
    const auto c = compareSections(track(), {}, track(0.5f), {}, kRate);
    assert(c.sections.size() == 3 && c.mostChanged == 1);
    assert(near(c.sections[1].levelDifferenceDb, -6.02, 0.5));
    assert(c.sections[1].spectralDistanceDb > 3.0);
    assert(std::abs(c.sections[0].levelDifferenceDb) < 0.5 && std::abs(c.sections[2].levelDifferenceDb) < 0.5);
    assert(c.sections[0].spectralDistanceDb < 1.0 && c.sections[2].spectralDistanceDb < 1.0);
}

void testAChangeInTheLastSectionIsFoundToo()
{
    const auto c = compareSections(track(), {}, track(1.0f, 440.0), {}, kRate);
    assert(c.mostChanged == 2 && c.sections[2].spectralDistanceDb > c.sections[0].spectralDistanceDb + 3.0);
}

void testWidthDifferenceNeedsBothStereo()
{
    // The same notes; in the second file the middle section's channels are unrelated.
    const auto left = track();
    auto wide = left;
    std::vector<float> other;
    append(other, 261.6256, 20.0, 0.2f);
    append(other, 587.3295, 20.0, 0.4f);
    for (std::size_t i = static_cast<std::size_t>(20 * kRate); i < static_cast<std::size_t>(40 * kRate); ++i)
        wide[i] = other[i];

    const auto c = compareSections(left, left, left, wide, kRate);
    assert(c.sections.size() == 3);
    assert(c.sections[1].hasWidth && c.sections[1].widthDifferenceDb > 20.0);
    assert(c.sections[0].hasWidth && std::abs(c.sections[0].widthDifferenceDb) < 1.0);

    // A mono first file has no width to compare.
    assert(! compareSections(left, {}, left, wide, kRate).sections[1].hasWidth);
}

void testALongerFirstFileOmitsWhatTheSecondLacks()
{
    auto shorter = track();
    shorter.resize(static_cast<std::size_t>(kRate * 30.0));   // ends in the middle section
    const auto c = compareSections(track(), {}, shorter, {}, kRate);
    assert(c.sections.size() == 2 && c.sections[1].endSeconds >= 30.0);
}

void testNothingToCompare()
{
    std::vector<float> short15;
    append(short15, 261.6256, 15.0, 0.2f);
    assert(compareSections(short15, {}, short15, {}, kRate).sections.empty());
    assert(compareSections({}, {}, track(), {}, kRate).sections.empty());
    assert(compareSections(track(), {}, {}, {}, kRate).sections.empty());
    assert(compareSections(track(), {}, track(), {}, 0.0).sections.empty());
    assert(compareSections(short15, {}, short15, {}, kRate).mostChanged == -1);
}

}  // namespace

int main()
{
    testIdenticalFilesDifferNowhere();
    testTheChangedSectionIsNamed();
    testAChangeInTheLastSectionIsFoundToo();
    testWidthDifferenceNeedsBothStereo();
    testALongerFirstFileOmitsWhatTheSecondLacks();
    testNothingToCompare();
    std::puts("shot_SectionCompareTest ok");
    return 0;
}

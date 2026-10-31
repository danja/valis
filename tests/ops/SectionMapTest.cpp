// tests/ops/SectionMapTest.cpp
//
// valis::findSections on tracks built from sections whose boundaries are known.

#include "valis/Analysis.h"
#include "valis/SectionMap.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
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

constexpr double kC4 = 261.6256, kG4 = 391.9954, kD5 = 587.3295;

/// C for 20 s, a louder G for 20 s, then C again.
std::vector<float> threeSections(double secondHz = kG4)
{
    std::vector<float> v;
    append(v, kC4, 20.0, 0.2f);
    append(v, secondHz, 20.0, 0.4f);
    append(v, kC4, 20.0, 0.2f);
    return v;
}

bool near(double a, double b, double within) { return std::abs(a - b) <= within; }

void testBoundariesAreFound()
{
    const auto map = findSections(threeSections(), kRate);
    assert(map.sections.size() == 3);
    assert(near(map.sections[0].endSeconds, 20.0, 2.0) && near(map.sections[1].endSeconds, 40.0, 2.0));
    assert(map.sections[2].endSeconds == 60.0);
    assert(map.sections[0].startSeconds == 0.0 && map.sections[1].startSeconds == map.sections[0].endSeconds);
    assert(map.novelty.size() == 60);
}

void testEachSectionIsDescribed()
{
    const auto map = findSections(threeSections(), kRate);
    assert(map.sections.size() == 3);
    assert(map.sections[0].dominantPitchClass == 0 && map.sections[1].dominantPitchClass == 7);
    assert(map.sections[2].dominantPitchClass == 0);
    assert(std::string(pitchClassName(7)) == "G" && std::string(pitchClassName(1)) == "C#");
    // The middle section is twice the amplitude: about 6 dB.
    assert(near(map.sections[1].energyDb - map.sections[0].energyDb, 6.02, 1.0));
    assert(! map.sections[0].hasStereo);
}

void testAnUnchangingTrackIsOneSection()
{
    std::vector<float> v;
    append(v, kC4, 40.0, 0.3f);
    const auto map = findSections(v, kRate);
    assert(map.sections.size() == 1 && map.sections[0].endSeconds == 40.0);
}

void testShortAndDegenerateInput()
{
    std::vector<float> v;
    append(v, kC4, 15.0, 0.3f);
    assert(findSections(v, kRate).sections.empty());
    assert(findSections({}, kRate).sections.empty());
    assert(findSections(threeSections(), 0.0).sections.empty());
    assert(findSections(threeSections(), 1000.0).sections.empty());   // a second is too few samples for a frame
}

void testStereoJoinsEachSection()
{
    // The middle section has unrelated channels; the others are centred.
    const auto left = threeSections();
    auto right = left;
    std::vector<float> other;
    append(other, kC4, 20.0, 0.2f);
    append(other, kD5, 20.0, 0.4f);
    for (std::size_t i = static_cast<std::size_t>(20 * kRate); i < static_cast<std::size_t>(40 * kRate); ++i)
        right[i] = other[i];

    const auto report = analyseSignal(left, right, kRate);
    const auto& sections = report.sectionMap.sections;
    assert(sections.size() == 3);
    assert(sections[0].hasStereo && sections[1].hasStereo);
    assert(sections[1].widthDb > sections[0].widthDb + 20.0);
    assert(sections[1].correlation < sections[0].correlation - 0.5);

    const auto json = reportToJson(report);
    assert(json.find("\"sections\":[{\"startS\":0") != std::string::npos && json.find("\"dominantNote\":\"C\"") != std::string::npos);
}

void testAShortReportHasNoSections()
{
    std::vector<float> v;
    append(v, kC4, 5.0, 0.3f);
    const auto report = analyseSignal(v, {}, kRate);
    assert(report.sectionMap.sections.empty());
    assert(reportToJson(report).find("\"sections\"") == std::string::npos);
}

void testAKeyIsRecoveredFromItsProfile()
{
    const std::array<double, 12> major = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    const std::array<double, 12> minor = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    for (int root = 0; root < 12; ++root)
    {
        std::array<double, 12> a{}, b{};
        for (int pc = 0; pc < 12; ++pc)
        {
            a[static_cast<std::size_t>(pc)] = 3.0 * major[static_cast<std::size_t>((pc - root + 12) % 12)] + 1.0;   // scale and offset do not matter
            b[static_cast<std::size_t>(pc)] = minor[static_cast<std::size_t>((pc - root + 12) % 12)];
        }
        const auto k1 = estimateKey(a), k2 = estimateKey(b);
        assert(k1.root == root && ! k1.minor && k1.correlation > 0.999);
        assert(k2.root == root && k2.minor && k2.correlation > 0.999);
    }
    assert(keyName({9, true, 1.0}) == "A minor" && keyName({6, false, 1.0}) == "F# major");

    // No pitch information, no key claimed.
    assert(estimateKey({}).correlation == 0.0 && keyName(estimateKey({})) == "none");
    std::array<double, 12> flat{};
    flat.fill(0.5);
    assert(estimateKey(flat).correlation == 0.0);
}

void testSectionKeysFromChords()
{
    auto chord = [](std::vector<float>& out, std::initializer_list<double> hz, double seconds) {
        std::vector<float> sum(static_cast<std::size_t>(kRate * seconds), 0.0f);
        for (const double f : hz)
        {
            std::vector<float> one;
            append(one, f, seconds, 0.12f);
            for (std::size_t i = 0; i < sum.size(); ++i) sum[i] += one[i];
        }
        out.insert(out.end(), sum.begin(), sum.end());
    };
    std::vector<float> v;
    chord(v, {261.63, 329.63, 392.0}, 20.0);    // C major
    chord(v, {369.99, 440.0, 554.37}, 20.0);    // F sharp minor
    chord(v, {261.63, 329.63, 392.0}, 20.0);

    const auto map = findSections(v, kRate);
    assert(map.sections.size() == 3);
    assert(map.sections[0].key.root == 0 && ! map.sections[0].key.minor);
    assert(map.sections[1].key.root == 6 && map.sections[1].key.minor);
    assert(keyName(map.sections[2].key) == "C major");
    assert(map.sections[0].key.correlation > 0.5);

    const auto json = reportToJson(analyseSignal(v, {}, kRate));
    assert(json.find("\"key\":\"C major\"") != std::string::npos && json.find("\"keyCorrelation\"") != std::string::npos);
}

}  // namespace

int main()
{
    testBoundariesAreFound();
    testEachSectionIsDescribed();
    testAnUnchangingTrackIsOneSection();
    testShortAndDegenerateInput();
    testStereoJoinsEachSection();
    testAShortReportHasNoSections();
    testAKeyIsRecoveredFromItsProfile();
    testSectionKeysFromChords();
    std::puts("ops_SectionMapTest ok");
    return 0;
}

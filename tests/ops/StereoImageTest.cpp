// tests/ops/StereoImageTest.cpp
//
// valis::analyseStereoImage: width and correlation for each second. Every signal here has a
// known image, so the test says what the figures must be.

#include "valis/Analysis.h"
#include "valis/StereoImage.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 24000.0;

std::vector<float> sine(double hz, double seconds, float amplitude = 0.4f)
{
    std::vector<float> out(static_cast<std::size_t>(kRate * seconds));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = amplitude * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
    return out;
}

std::vector<float> negated(std::vector<float> v)
{
    for (auto& s : v) s = -s;
    return v;
}

void testIdenticalChannelsAreMono()
{
    const auto tone = sine(330.0, 3.0);
    const auto image = analyseStereoImage(tone, tone, kRate);
    assert(image.seconds.size() == 3 && image.validSeconds == 3);
    for (const auto& s : image.seconds)
        assert(s.valid && s.widthDb < -80.0 && s.correlation > 0.999);
    assert(image.anomalies.empty());
}

void testUnrelatedChannelsAreWide()
{
    const auto image = analyseStereoImage(sine(330.0, 2.0), sine(550.0, 2.0), kRate);
    for (const auto& s : image.seconds)
        assert(s.valid && std::abs(s.widthDb) < 1.0 && std::abs(s.correlation) < 0.05);
}

void testOppositeChannelsHaveNoMidToMeasureAgainst()
{
    const auto tone = sine(330.0, 2.0);
    const auto image = analyseStereoImage(tone, negated(tone), kRate);
    assert(image.validSeconds == 0 && ! image.seconds[0].valid);
}

void testBandsSeparate()
{
    // Bass in both channels alike, treble in opposite phase: the bass is mid, the treble is side.
    const auto bass = sine(100.0, 2.0), treble = sine(6000.0, 2.0, 0.2f);
    std::vector<float> left(bass.size()), right(bass.size());
    for (std::size_t i = 0; i < bass.size(); ++i)
    {
        left[i] = bass[i] + treble[i];
        right[i] = bass[i] - treble[i];
    }
    const auto image = analyseStereoImage(left, right, kRate);
    assert(image.validSeconds == 2);
    assert(image.lowDb < -40.0);
    assert(image.highDb > 40.0);
}

void testSilenceAndShortSignals()
{
    const std::vector<float> silence(static_cast<std::size_t>(kRate * 2.0), 0.0f);
    const auto quiet = analyseStereoImage(silence, silence, kRate);
    assert(quiet.seconds.size() == 2 && quiet.validSeconds == 0 && quiet.anomalies.empty());

    const auto tone = sine(330.0, 0.5);
    assert(analyseStereoImage(tone, tone, kRate).seconds.empty());
    assert(analyseStereoImage({}, {}, kRate).seconds.empty());
    assert(analyseStereoImage(tone, tone, 0.0).seconds.empty());
}

void testAWideSectionIsFound()
{
    // Twenty seconds in the centre, then ten seconds with unrelated channels.
    auto left = sine(330.0, 20.0), right = left;
    const auto wideL = sine(330.0, 10.0), wideR = sine(550.0, 10.0);
    left.insert(left.end(), wideL.begin(), wideL.end());
    right.insert(right.end(), wideR.begin(), wideR.end());

    const auto image = analyseStereoImage(left, right, kRate);
    assert(image.seconds.size() == 30);
    assert(image.anomalies.size() == 1);
    assert(image.anomalies[0].startSeconds == 20.0 && image.anomalies[0].endSeconds == 30.0);
    assert(image.anomalies[0].reason.find("width") != std::string::npos);

    // The same finding reaches a caller of analyseSignal, in the notes and the JSON.
    const auto report = analyseSignal(left, right, kRate);
    assert(std::any_of(report.notes.begin(), report.notes.end(),
                       [](const std::string& n) { return n.find("Stereo image changes from 20") != std::string::npos; }));
    const auto json = reportToJson(report);
    assert(json.find("\"stereoImage\"") != std::string::npos && json.find("\"widthDbPerSecond\"") != std::string::npos);
}

void testAMonoReportHasNoImage()
{
    const auto report = analyseSignal(sine(330.0, 2.0), {}, kRate);
    assert(report.stereoImage.seconds.empty());
    assert(reportToJson(report).find("stereoImage") == std::string::npos);
}

}  // namespace

int main()
{
    testIdenticalChannelsAreMono();
    testUnrelatedChannelsAreWide();
    testOppositeChannelsHaveNoMidToMeasureAgainst();
    testBandsSeparate();
    testSilenceAndShortSignals();
    testAWideSectionIsFound();
    testAMonoReportHasNoImage();
    std::puts("ops_StereoImageTest ok");
    return 0;
}

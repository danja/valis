// tests/ops/TempoEstimateTest.cpp
//
// valis::estimateTempo on pulse trains whose tempo is known.

#include "valis/Analysis.h"
#include "valis/TempoEstimate.h"

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

/// A short decaying 1 kHz burst on each beat. `accent` scales every fourth, as a bar's downbeat would be.
std::vector<float> clicks(double bpm, double seconds, float accent = 1.0f)
{
    std::vector<float> out(static_cast<std::size_t>(kRate * seconds), 0.0f);
    const double period = 60.0 / bpm * kRate;
    const std::size_t burst = static_cast<std::size_t>(0.02 * kRate);
    int beat = 0;
    for (double at = 0.1 * kRate; at + static_cast<double>(burst) < static_cast<double>(out.size()); at += period, ++beat)
    {
        const float level = 0.5f * (beat % 4 == 0 ? accent : 1.0f);
        for (std::size_t i = 0; i < burst; ++i)
            out[static_cast<std::size_t>(at) + i] += level * std::exp(-static_cast<float>(i) / (0.005f * static_cast<float>(kRate)))
                                                    * static_cast<float>(std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / kRate));
    }
    return out;
}

bool near(double a, double b, double within) { return std::abs(a - b) <= within; }

void testSeveralTemposAreFound()
{
    for (const double bpm : {70.0, 90.0, 120.0, 128.0})
    {
        const auto t = estimateTempo(clicks(bpm, 30.0), kRate);
        assert(t.found && near(t.bpm(), bpm, 0.01 * bpm));
        assert(t.candidates.front().strength > 0.1);
    }

    // At 174 the half tempo, 87, is about as likely a beat; either may come first, and both are offered.
    const auto fast = estimateTempo(clicks(174.0, 30.0), kRate);
    const auto has = [&](double bpm) {
        return std::any_of(fast.candidates.begin(), fast.candidates.end(), [&](const TempoCandidate& c) { return near(c.bpm, bpm, 0.01 * bpm); });
    };
    assert(fast.found && has(174.0) && has(87.0));
}

void testAnAccentedBarKeepsTheBeat()
{
    const auto t = estimateTempo(clicks(120.0, 40.0, 2.0f), kRate);
    assert(t.found && near(t.bpm(), 120.0, 1.5));
}

void testCandidatesAreDistinct()
{
    const auto t = estimateTempo(clicks(100.0, 40.0), kRate);
    assert(t.candidates.size() >= 2 && t.candidates.size() <= 4);
    for (std::size_t i = 1; i < t.candidates.size(); ++i)
    {
        for (std::size_t j = 0; j < i; ++j)
            assert(std::abs(t.candidates[i].bpm - t.candidates[j].bpm) >= 0.03 * t.candidates[j].bpm);
    }
    for (const auto& c : t.candidates)
        assert(c.bpm >= 40.0 && c.bpm <= 200.0);
}

void testNoPulseMeansNoTempo()
{
    assert(! estimateTempo(std::vector<float>(static_cast<std::size_t>(kRate * 20.0), 0.0f), kRate).found);
    assert(! estimateTempo(clicks(120.0, 5.0), kRate).found);    // under 8 s
    assert(! estimateTempo({}, kRate).found);
    assert(! estimateTempo(clicks(120.0, 20.0), 0.0).found);

    // A steady tone has no onsets to repeat.
    std::vector<float> tone(static_cast<std::size_t>(kRate * 20.0));
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.4f * static_cast<float>(std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / kRate));
    assert(! estimateTempo(tone, kRate).found);
}

void testTheReportCarriesIt()
{
    const auto report = analyseSignal(clicks(120.0, 30.0), {}, kRate);
    assert(report.tempo.found && near(report.tempo.bpm(), 120.0, 1.5));
    const auto json = reportToJson(report);
    assert(json.find("\"tempo\":{\"bpm\":") != std::string::npos && json.find("\"candidates\":[{\"bpm\"") != std::string::npos);

    const auto quiet = analyseSignal(std::vector<float>(static_cast<std::size_t>(kRate * 3.0), 0.0f), {}, kRate);
    assert(reportToJson(quiet).find("\"tempo\"") == std::string::npos);
}

}  // namespace

int main()
{
    testSeveralTemposAreFound();
    testAnAccentedBarKeepsTheBeat();
    testCandidatesAreDistinct();
    testNoPulseMeansNoTempo();
    testTheReportCarriesIt();
    std::puts("ops_TempoEstimateTest ok");
    return 0;
}

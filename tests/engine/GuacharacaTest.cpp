// tests/engine/GuacharacaTest.cpp
//
// circuits/guacharaca.ttl (scripts/latin/guacharaca.py), played through the engine: silent until a stroke, the down
// and up strokes differ, and the controls do what the header says. Ticks are counted as peaks of the smoothed envelope,
// so Down teeth is read as a rate and Regularity as how evenly the ticks fall.

#include "ExampleRig.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using valis::test::ExampleRig;

namespace {

constexpr double kRate = ExampleRig::kRate;

struct Ticks
{
    int count = 0;
    double spread = 0.0;   ///< standard deviation of the gaps over their mean
    double peak = 0.0;
};

Ticks stroke(int note, const std::vector<std::pair<const char*, float>>& settings)
{
    ExampleRig rig("guacharaca.ttl");
    for (const auto& [symbol, value] : settings)
        rig.setParam(symbol, value);
    rig.engine.noteOn(note, 1.0f);
    const auto x = rig.render(0.6);

    Ticks t;
    const std::size_t k = static_cast<std::size_t>(0.0005 * kRate);
    std::vector<double> env(x.size(), 0.0);
    double run = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        assert(std::isfinite(x[i]));
        t.peak = std::max(t.peak, static_cast<double>(std::fabs(x[i])));
        run += (std::fabs(x[i]) - run) / static_cast<double>(k);
        env[i] = run;
    }
    const double top = *std::max_element(env.begin(), env.end());
    std::vector<double> at;
    for (std::size_t i = 1; i + 1 < env.size(); ++i)
        if (env[i] > env[i - 1] && env[i] >= env[i + 1] && env[i] > 0.1 * top
            && (at.empty() || static_cast<double>(i) - at.back() > 0.003 * kRate))
            at.push_back(static_cast<double>(i));
    t.count = static_cast<int>(at.size());
    if (at.size() > 2)
    {
        std::vector<double> gap;
        for (std::size_t i = 1; i < at.size(); ++i)
            gap.push_back(at[i] - at[i - 1]);
        double mean = 0.0, var = 0.0;
        for (const double g : gap) mean += g;
        mean /= static_cast<double>(gap.size());
        for (const double g : gap) var += (g - mean) * (g - mean);
        t.spread = std::sqrt(var / static_cast<double>(gap.size())) / mean;
    }
    return t;
}

}  // namespace

int main()
{
    {   // silent until played
        ExampleRig rig("guacharaca.ttl");
        for (const float v : rig.render(0.5))
            assert(v == 0.0f);
    }

    const auto down = stroke(73, {});
    const auto up = stroke(74, {});
    std::printf("down: %d ticks, spread %.2f, peak %.3f; up: %d ticks, peak %.3f\n", down.count, down.spread, down.peak, up.count, up.peak); std::fflush(stdout);
    assert(down.peak > 0.1 && down.peak < 0.99);   // audible, and under the limiter
    assert(up.peak > 0.03);
    assert(down.peak > up.peak);                   // the down stroke is the stronger
    assert(down.count >= 8);                       // a scrape, not a click or a hiss

    // Down teeth: twice the teeth, about twice the ticks
    const auto slow = stroke(73, {{"down_teeth", 20.0f}});
    const auto fast = stroke(73, {{"down_teeth", 50.0f}});
    std::printf("teeth 20: %d ticks, teeth 50: %d ticks\n", slow.count, fast.count); std::fflush(stdout);
    assert(fast.count > slow.count * 3 / 2);

    // Regularity: even ticks at 1, random ones at 0
    const auto even = stroke(73, {{"regularity", 1.0f}});
    const auto loose = stroke(73, {{"regularity", 0.0f}});
    std::printf("regularity 1: spread %.2f, regularity 0: spread %.2f\n", even.spread, loose.spread); std::fflush(stdout);
    assert(even.spread < loose.spread);

    // Down level is the level of the down stroke alone
    const auto quiet = stroke(73, {{"down_level", 6.0f}});
    const auto uplift = stroke(74, {{"down_level", 6.0f}});
    assert(quiet.peak < down.peak * 0.5);
    assert(std::fabs(uplift.peak - up.peak) < 1e-6);
    return 0;
}

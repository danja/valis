// tests/dsp/HardClipTest.cpp
//
// val:HardClip cuts the signal flat at plus and minus val:threshold and leaves what is below it alone. It once
// treated the threshold as a gain in front of a fixed clip at 1, so a lower threshold made the output louder in
// proportion and a quiet signal was never passed at unit level.

#include "valis/ElementTestFixture.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace valis;

namespace {

float peakAfterSettling(const std::vector<float>& x)
{
    float peak = 0.0f;
    for (std::size_t i = x.size() / 2; i < x.size(); ++i)
        peak = std::max(peak, std::abs(x[i]));
    return peak;
}

}  // namespace

int main()
{
    for (const float threshold : {0.1f, 0.5f, 1.0f, 1.5f})
    {
        ElementTestFixture rig("HardClip");
        rig.set("threshold", threshold);

        // far above the threshold: flat at the threshold, whatever it is
        const auto loud = rig.run(ElementTestFixture::generateSine(200.0, 48000.0, 4800, 4.0f * threshold + 1.0f));
        assert(std::abs(peakAfterSettling(loud) - threshold) < 0.01f * threshold + 1e-4f);

        // well below it: passed at unit level, to within the one-sample delay of the anti-aliasing
        rig.reset();
        const auto quiet = rig.run(ElementTestFixture::generateSine(200.0, 48000.0, 4800, 0.2f * threshold));
        assert(std::abs(peakAfterSettling(quiet) - 0.2f * threshold) < 0.01f * threshold);
    }

    std::puts("HardClipTest passed");
    return 0;
}

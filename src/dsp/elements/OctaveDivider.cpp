// src/dsp/elements/OctaveDivider.cpp
//
// val:OctaveDivider is the analogue octave-down effect: a Schmitt trigger squares the input, a
// flip-flop halves the frequency of the square wave (a second flip-flop halves it again), and the
// result is given the loudness of the input, so it dies away with the note as the input does.
// https://en.wikipedia.org/wiki/Frequency_divider
//
// The trigger switches at plus and minus a fraction of the input's own recent peak, not at fixed
// levels, so a quiet note and a loud one divide alike and the noise between notes does not toggle
// it. Like every divider of this kind it is for one note at a time: a chord or a bright, noisy
// sound crosses the thresholds at the wrong moments and the output is a growl.

#include "Common.h"

#include <cmath>

namespace valis::elements {

class OctaveDivider final : public MonoElement
{
protected:
    void cacheIndices(const ElementType& type) override
    {
        divisionIndex  = controlIndex(type, "division");
        hysteresisIndex = controlIndex(type, "hysteresis");
        dryIndex       = controlIndex(type, "dry");
        wetIndex       = controlIndex(type, "wet");
    }

    void reset() override
    {
        peak = 0.0f;
        level = 0.0f;
        high = false;
        half = false;
        quarter = false;
    }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const bool  twoOctaves = controlAt(args, divisionIndex, 0.0f) > 0.5f;
        const float hysteresis = std::clamp(controlAt(args, hysteresisIndex, 0.25f), 0.02f, 0.9f);
        const float dry        = std::clamp(controlAt(args, dryIndex, 0.0f), 0.0f, 1.0f);
        const float wet        = std::clamp(controlAt(args, wetIndex, 1.0f), 0.0f, 1.0f);

        const float peakRise  = timeToCoeff(0.5f, sampleRate);
        const float peakFall  = timeToCoeff(60.0f, sampleRate);
        const float levelRise = timeToCoeff(3.0f, sampleRate);
        const float levelFall = timeToCoeff(30.0f, sampleRate);

        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            const float magnitude = std::abs(x);

            peak  = magnitude + (magnitude > peak ? peakRise : peakFall) * (peak - magnitude);
            level = magnitude + (magnitude > level ? levelRise : levelFall) * (level - magnitude);

            // Followers that have decayed to nothing are set to nothing, so they never go subnormal.
            if (peak < 1.0e-20f) peak = 0.0f;
            if (level < 1.0e-20f) level = 0.0f;

            // Below a floor the input is silence, not a note, and does not trigger.
            const float threshold = std::max(peak * hysteresis, 1.0e-4f);
            bool rose = false;
            if (! high && x > threshold)
            {
                high = true;
                rose = true;
            }
            else if (high && x < -threshold)
                high = false;

            if (rose)
            {
                half = ! half;
                if (half)
                    quarter = ! quarter;
            }

            const bool state = twoOctaves ? quarter : half;
            // A square wave has an rms equal to its peak, a sine only 0.7 of it, and the follower sits near the
            // input peak, so the follower is scaled down to bring the two to about the same loudness.
            const float square = (state ? 1.0f : -1.0f) * std::min(level * 0.75f, 1.0f);
            out[i] = x * dry + square * wet;
        }
    }

private:
    float peak = 0.0f, level = 0.0f;
    bool  high = false, half = false, quarter = false;
    int   divisionIndex = -1, hysteresisIndex = -1, dryIndex = -1, wetIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerOctaveDivider(ElementRegistry& registry)
{
    registry.add("OctaveDivider", &make<elements::OctaveDivider>);
}

}  // namespace valis

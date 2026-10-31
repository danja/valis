// src/dsp/elements/Reverb.cpp
//
// A feedback delay network: eight delay lines whose outputs are mixed by an
// orthogonal matrix and fed back to their inputs (Jot and Chaigne, 1991). An
// orthogonal mix loses no energy, so the decay is set entirely by the gain on
// each line, and the network cannot grow whatever the delay lengths are.
//
// Everything is sized in prepare(); process() only reads and writes the lines.

#include "Common.h"

#include <array>
#include <cmath>
#include <vector>

namespace valis::elements {

/// Eight-line feedback delay network with four allpass diffusers on the input.
///
/// Each line's gain is chosen from its own length so that every path through
/// the network falls 60 dB in `decay` seconds, whatever `size` is. Damping is a
/// one-pole lowpass in each line, which has unity gain at DC, so it shortens
/// only the high frequencies and `decay` stays the low-frequency time.
class Reverb final : public DspElement
{
    static constexpr std::size_t kLines        = 8;
    static constexpr std::size_t kDiffusers    = 4;
    static constexpr float kMaxSize      = 2.0f;
    static constexpr float kDiffuserGain = 0.6f;

    // Mutually prime lengths at 44.1 kHz, scaled to the engine's rate.
    static constexpr std::array<int, kLines>     kLineBase     { 1087, 1283, 1487, 1721, 1987, 2281, 2593, 2909 };
    static constexpr std::array<int, kDiffusers> kDiffuserBase { 223, 347, 467, 599 };

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate   = rate;
        sizeIndex    = controlIndex(type, "size");
        decayIndex   = controlIndex(type, "decay");
        dampingIndex = controlIndex(type, "damping");
        mixIndex     = controlIndex(type, "mix");
        rightOut     = audioOutIndex(type, "right");

        const double scale = rate / 44100.0;
        for (std::size_t i = 0; i < kLines; ++i)
        {
            // Whole samples at size 1: linear interpolation of a fractional delay is
            // a lowpass, which would shorten the tail below the time asked for.
            baseLength[i] = std::round(static_cast<float>(kLineBase[i] * scale));
            line[i].assign(static_cast<std::size_t>(baseLength[i] * kMaxSize) + 4, 0.0f);
        }
        for (std::size_t i = 0; i < kDiffusers; ++i)
        {
            diffuserLength[i] = std::max(8, static_cast<int>(kDiffuserBase[i] * scale));
            diffuser[i].assign(static_cast<std::size_t>(diffuserLength[i]), 0.0f);
        }
        smoothedSize = -1.0f;
        reset();
    }

    void reset() override
    {
        for (auto& l : line)     std::fill(l.begin(), l.end(), 0.0f);
        for (auto& d : diffuser) std::fill(d.begin(), d.end(), 0.0f);
        writePos.fill(0);
        diffuserPos.fill(0);
        damped.fill(0.0f);
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float sizeTarget = std::clamp(controlAt(args, sizeIndex, 1.0f), 0.1f, kMaxSize);
        const float decay      = std::clamp(controlAt(args, decayIndex, 2.0f), 0.1f, 20.0f);
        const float damping    = std::clamp(controlAt(args, dampingIndex, 0.3f), 0.0f, 1.0f);
        const float mix        = std::clamp(controlAt(args, mixIndex, 0.3f), 0.0f, 1.0f);

        // The first block starts at the asked size; later ones glide to it, so a
        // moving knob changes the read points smoothly instead of with a click.
        if (smoothedSize < 0.0f)
            smoothedSize = sizeTarget;

        // A line that sits still is read at a whole number of samples. A fractional read
        // interpolates, and linear interpolation is a lowpass, so the tail would fall a
        // little faster than asked at every size that is not a whole number of samples.
        // Only a line that is moving needs the fraction.
        const bool steady = std::abs(sizeTarget - smoothedSize) <= 0.0f;

        const float sizeStep = (sizeTarget - smoothedSize) / static_cast<float>(std::max(args.numSamples, 1));

        const float lowpass = 1.0f - 0.95f * damping;

        // -60 dB after `decay` seconds: gain = 0.001 ^ (delay / (decay * rate)).
        // Taken at the block's end size, since it moves by less than a block's worth.
        std::array<float, kLines> gain;
        const float endSize = sizeTarget;
        for (std::size_t i = 0; i < kLines; ++i)
        {
            const float lap = steady ? std::round(baseLength[i] * endSize) : baseLength[i] * endSize;
            gain[i] = std::exp(-6.907755f * lap / (decay * static_cast<float>(sampleRate)));
        }

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];
        float*       right = rightOut >= 0 && rightOut < args.numAudioOut ? args.audioOut[rightOut] : nullptr;

        for (int n = 0; n < args.numSamples; ++n)
        {
            smoothedSize += sizeStep;

            float x = in[n];
            for (std::size_t d = 0; d < kDiffusers; ++d)
            {
                auto& buf = diffuser[d];
                int&  pos = diffuserPos[d];
                const float delayed = buf[static_cast<std::size_t>(pos)];
                const float v = x - kDiffuserGain * delayed;
                buf[static_cast<std::size_t>(pos)] = v;
                x = delayed + kDiffuserGain * v;
                if (++pos >= diffuserLength[d])
                    pos = 0;
            }

            std::array<float, kLines> tap;
            for (std::size_t i = 0; i < kLines; ++i)
            {
                auto& buf = line[i];
                const int len = static_cast<int>(buf.size());

                float delay = std::min(baseLength[i] * smoothedSize, static_cast<float>(len - 3));
                if (steady)
                    delay = std::round(delay);
                const int   whole = static_cast<int>(delay);
                const float frac  = delay - static_cast<float>(whole);

                int r0 = writePos[i] - whole;
                if (r0 < 0) r0 += len;
                int r1 = r0 - 1;
                if (r1 < 0) r1 += len;

                const float read = buf[static_cast<std::size_t>(r0)] * (1.0f - frac)
                                 + buf[static_cast<std::size_t>(r1)] * frac;

                damped[i] += lowpass * (read - damped[i]);
                tap[i] = damped[i];
            }

            // Orthonormal 8x8 Hadamard transform: three butterfly passes, then 1/sqrt(8).
            std::array<float, kLines> h = tap;
            for (std::size_t span = 1; span < kLines; span *= 2)
                for (std::size_t base = 0; base < kLines; base += span * 2)
                    for (std::size_t k = base; k < base + span; ++k)
                    {
                        const float a = h[k];
                        const float b = h[k + span];
                        h[k]        = a + b;
                        h[k + span] = a - b;
                    }

            constexpr float kNorm = 0.35355339f;
            for (std::size_t i = 0; i < kLines; ++i)
            {
                auto& buf = line[i];
                buf[static_cast<std::size_t>(writePos[i])] = x * kNorm + h[i] * kNorm * gain[i];
                if (++writePos[i] >= static_cast<int>(buf.size()))
                    writePos[i] = 0;
            }

            // Two rows of the Hadamard matrix, each orthogonal to the feedback sum (the
            // first row) and to each other, so the two outputs are not copies of one
            // tail: alternating signs for `out`, pairs of signs for `right`.
            float wetOut = 0.0f, wetRight = 0.0f;
            for (std::size_t i = 0; i < kLines; ++i)
            {
                wetOut   += (i & 1) ? -tap[i] : tap[i];
                wetRight += (i & 2) ? -tap[i] : tap[i];
            }

            out[n] = in[n] * (1.0f - mix) + wetOut * kNorm * 2.0f * mix;
            if (right != nullptr)
                right[n] = in[n] * (1.0f - mix) + wetRight * kNorm * 2.0f * mix;
        }

        // The glide ends here, exactly, so the next block can tell a resting size.
        smoothedSize = sizeTarget;
    }

private:
    double sampleRate = 44100.0;
    int sizeIndex = -1, decayIndex = -1, dampingIndex = -1, mixIndex = -1, rightOut = -1;

    std::array<std::vector<float>, kLines>     line;
    std::array<std::vector<float>, kDiffusers> diffuser;
    std::array<float, kLines> baseLength{};
    std::array<int, kDiffusers> diffuserLength{};
    std::array<int, kLines> writePos{};
    std::array<int, kDiffusers> diffuserPos{};
    std::array<float, kLines> damped{};
    float smoothedSize = -1.0f;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerReverb(ElementRegistry& registry)
{
    registry.add("Reverb", &make<elements::Reverb>);
}

}  // namespace valis

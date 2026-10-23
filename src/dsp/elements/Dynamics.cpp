// src/dsp/elements/Dynamics.cpp
//
// Detector and gain computer are separate elements, so a sidechain is a visible
// arc rather than a coupling hidden inside a compressor. Scream gates its
// feedback path from an envelope of the input, which only works that way.
//
// Compression curves follow Giannoulis, Massberg and Reiss (JAES 2012).

#include "Common.h"

#include <vector>

namespace valis::elements {

/// Peak or RMS detection with independent attack and release. Emits one value
/// per block on a control output.
class EnvelopeFollower final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate   = rate;
        attackIndex  = controlIndex(type, "attack");
        releaseIndex = controlIndex(type, "release");
        modeIndex    = controlIndex(type, "mode");
        reset();
    }

    void reset() override { envelope = 0.0f; }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numControlOut < 1)
            return;

        const float attack  = timeToCoeff(controlAt(args, attackIndex, 1.0f), sampleRate);
        const float release = timeToCoeff(controlAt(args, releaseIndex, 100.0f), sampleRate);
        const bool  rms     = controlAt(args, modeIndex, 0.0f) > 0.5f;

        const float* in = args.audioIn[0];
        for (int i = 0; i < args.numSamples; ++i)
        {
            const float x = rms ? in[i] * in[i] : std::abs(in[i]);
            const float coeff = x > envelope ? attack : release;
            envelope = x + coeff * (envelope - x);
        }

        args.controlOut[0] = rms ? std::sqrt(std::max(envelope, 0.0f)) : envelope;
    }

private:
    double sampleRate = 44100.0;
    float envelope = 0.0f;
    int attackIndex = -1, releaseIndex = -1, modeIndex = -1;
};

/// Hard-knee downward expander, used as a gate. Its amount port takes the
/// detector level through a control arc.
class Expander final : public MonoElement
{
protected:
    void cacheIndices(const ElementType& type) override
    {
        amountIndex    = controlIndex(type, "amount");
        thresholdIndex = controlIndex(type, "threshold");
        ratioIndex     = controlIndex(type, "ratio");
    }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const float level     = controlAt(args, amountIndex, 0.0f);
        const float threshold = controlAt(args, thresholdIndex, -60.0f);
        const float ratio     = std::max(controlAt(args, ratioIndex, 2.0f), 1.0f);

        const float levelDb = gainToDb(level);

        // Below the threshold the signal is pushed further down; above it the
        // expander is transparent.
        float gain = 1.0f;
        if (levelDb < threshold)
        {
            const float reductionDb = (levelDb - threshold) * (ratio - 1.0f);
            gain = reductionDb < -140.0f ? 0.0f : dbToGain(reductionDb);
        }

        for (int i = 0; i < n; ++i)
            out[i] = in[i] * gain;
    }

private:
    int amountIndex = -1, thresholdIndex = -1, ratioIndex = -1;
};

/// Soft-knee compressor with an optional upward stage. Setting upward above
/// zero lifts quiet material toward the threshold, which is what gives an
/// OTT-style sound its density.
class Compressor final : public MonoElement
{
public:
    void reset() override { envelope = 0.0f; }

protected:
    void cacheIndices(const ElementType& type) override
    {
        thresholdIndex = controlIndex(type, "threshold");
        ratioIndex     = controlIndex(type, "ratio");
        kneeIndex      = controlIndex(type, "knee");
        attackIndex    = controlIndex(type, "attack");
        releaseIndex   = controlIndex(type, "release");
        upwardIndex    = controlIndex(type, "upward");
        boostIndex     = controlIndex(type, "boost");
    }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const float threshold = controlAt(args, thresholdIndex, -6.0f);
        const float ratio     = std::max(controlAt(args, ratioIndex, 4.0f), 1.0f);
        const float knee      = std::max(controlAt(args, kneeIndex, 6.0f), 0.0f);
        const float attack    = timeToCoeff(controlAt(args, attackIndex, 5.0f), sampleRate);
        const float release   = timeToCoeff(controlAt(args, releaseIndex, 100.0f), sampleRate);
        const float upward    = std::clamp(controlAt(args, upwardIndex, 0.0f), 0.0f, 1.0f);
        const float maxBoost  = std::max(controlAt(args, boostIndex, 18.0f), 0.0f);

        const float ratioInv = 1.0f / ratio;

        for (int i = 0; i < n; ++i)
        {
            const float x = std::abs(in[i]);
            const float coeff = x > envelope ? attack : release;
            envelope = x + coeff * (envelope - x);

            const float levelDb = gainToDb(envelope);
            float targetDb = levelDb;

            // Downward: soft knee around the threshold.
            const float over = levelDb - threshold;
            if (knee > 0.0f && std::abs(over) <= knee * 0.5f)
            {
                const float t = over + knee * 0.5f;
                targetDb = levelDb + (ratioInv - 1.0f) * t * t / (2.0f * knee);
            }
            else if (over > 0.0f)
            {
                targetDb = threshold + over * ratioInv;
            }

            // Upward: lift what sits below the threshold, by no more than maxBoost,
            // or the noise floor of a quiet passage would be lifted a hundred decibels.
            if (upward > 0.0f && levelDb < threshold)
                targetDb += std::min(upward * (threshold - levelDb) * (1.0f - ratioInv), maxBoost);

            out[i] = in[i] * dbToGain(targetDb - levelDb);
        }
    }

private:
    float envelope = 0.0f;
    int thresholdIndex = -1, ratioIndex = -1, kneeIndex = -1;
    int attackIndex = -1, releaseIndex = -1, upwardIndex = -1, boostIndex = -1;
};

/// ADSR contour on a control output, driven by note events.
///
/// Advances a stage at a time on the control grid, so its shape does not depend
/// on the host's buffer size any more than the rest of the circuit does.
class Envelope final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate   = rate;
        attackIndex  = controlIndex(type, "attack");
        decayIndex   = controlIndex(type, "decay");
        sustainIndex = controlIndex(type, "sustain");
        releaseIndex = controlIndex(type, "release");
        gateIndex    = controlIndex(type, "gate");
        reset();
    }

    void reset() override
    {
        level = 0.0f;
        stage = Stage::idle;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 1)
            return;

        const float sustain  = std::clamp(controlAt(args, sustainIndex, 0.7f), 0.0f, 1.0f);

        // A connected gate control input (e.g. from NoteGate) overrides the host
        // MIDI gate; sentinel -1.0 means fall back to args.gate.
        const float gateCtrl = controlAt(args, gateIndex, -1.0f);
        const bool  gateNow  = gateCtrl >= 0.0f ? gateCtrl >= 0.5f : args.gate;

        if (gateNow && ! wasGated)
            stage = Stage::attack;
        else if (! gateNow && wasGated)
            stage = Stage::release;

        wasGated = gateNow;

        const auto blockCoeff = [&](int index, float fallback)
        {
            const float ms = std::max(controlAt(args, index, fallback), 0.01f);
            return std::pow(timeToCoeff(ms, sampleRate), static_cast<float>(args.numSamples));
        };

        switch (stage)
        {
            case Stage::attack:
            {
                // Aim above 1 so the curve crosses it rather than approaching
                // it asymptotically, which is what makes an attack feel finite.
                const float coeff = blockCoeff(attackIndex, 10.0f);
                level = 1.2f + coeff * (level - 1.2f);
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::decay;
                }
                break;
            }

            case Stage::decay:
            {
                const float coeff = blockCoeff(decayIndex, 200.0f);
                level = sustain + coeff * (level - sustain);
                if (std::abs(level - sustain) < 1.0e-4f)
                {
                    level = sustain;
                    stage = Stage::sustain;
                }
                break;
            }

            case Stage::sustain:
                level = sustain;
                break;

            case Stage::release:
            {
                const float coeff = blockCoeff(releaseIndex, 300.0f);
                level *= coeff;
                if (level < 1.0e-4f)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                break;
            }

            case Stage::idle:
                level = 0.0f;
                break;
        }

        args.controlOut[0] = level;
    }

private:
    enum class Stage { idle, attack, decay, sustain, release };

    double sampleRate = 44100.0;
    float level = 0.0f;
    bool wasGated = false;
    Stage stage = Stage::idle;
    int attackIndex = -1, decayIndex = -1, sustainIndex = -1, releaseIndex = -1, gateIndex = -1;
};

/// Lookahead brickwall limiter. No sample leaves above the ceiling.
///
/// The gain a sample needs is the ceiling over its size. The lowest of those across a
/// window of L samples, averaged over L samples, reaches that gain before the sample
/// does and moves to it in a ramp, so a peak is met by a smooth fall in gain rather than
/// a snap. The audio is delayed by L - 1 samples to line up with the gain, and that is
/// declared as latency. Recovery after a peak is an exponential release.
///
/// The optional link input is a second signal the detector also listens to. Patch each
/// side of a stereo pair into the other's link and both compute the gain from the louder
/// of the two, so both are turned down by the same amount and the image does not move.
class Limiter final : public DspElement
{
    static constexpr double kLookaheadMs = 1.5;

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate   = rate;
        ceilingIndex = controlIndex(type, "ceiling");
        releaseIndex = controlIndex(type, "release");

        window = std::max<std::size_t>(2, static_cast<std::size_t>(std::lround(kLookaheadMs * 0.001 * rate)));
        delayed.assign(window, 0.0f);
        average.assign(window, 1.0f);
        minValue.assign(window + 1, 1.0f);
        minIndex.assign(window + 1, 0);
        reset();
    }

    void reset() override
    {
        std::fill(delayed.begin(), delayed.end(), 0.0f);
        std::fill(average.begin(), average.end(), 1.0f);
        sum        = static_cast<double>(window);
        position   = 0;
        front      = 0;
        count      = 0;
        sampleTime = 0;
        gain       = 1.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float ceiling = dbToGain(std::clamp(controlAt(args, ceilingIndex, -0.3f), -24.0f, 0.0f));
        const float release = timeToCoeff(controlAt(args, releaseIndex, 60.0f), sampleRate);

        const float* in   = args.audioIn[0];
        const float* link = args.numAudioIn > 1 ? args.audioIn[1] : nullptr;
        float*       out  = args.audioOut[0];
        const std::size_t capacity = minValue.size();

        for (int i = 0; i < args.numSamples; ++i)
        {
            const float magnitude = link != nullptr ? std::max(std::abs(in[i]), std::abs(link[i]))
                                                    : std::abs(in[i]);
            const float needed    = magnitude > ceiling ? ceiling / magnitude : 1.0f;

            // Sliding minimum of the needed gain: a deque kept in ascending order.
            while (count > 0 && minValue[(front + count - 1) % capacity] >= needed)
                --count;
            const std::size_t back = (front + count) % capacity;
            minValue[back] = needed;
            minIndex[back] = sampleTime;
            ++count;
            while (minIndex[front] + static_cast<std::int64_t>(window) <= sampleTime)
            {
                front = (front + 1) % capacity;
                --count;
            }

            // Moving average of that minimum. The terms are floats added to a double,
            // so the sum is exact and does not drift.
            const float lowest = minValue[front];
            sum += static_cast<double>(lowest) - static_cast<double>(average[position]);
            average[position] = lowest;
            const float target = static_cast<float>(sum / static_cast<double>(window));

            // Down at once, since the average is already a ramp; up by release. The
            // gain never exceeds the average, so the ceiling holds.
            gain = target < gain ? target : target + release * (gain - target);

            // The oldest sample in the line is window - 1 samples old: the one the
            // gain was computed for.
            const std::size_t oldest = (position + 1) % window;
            const float held = delayed[oldest];
            delayed[position] = in[i];
            position = oldest;

            out[i] = std::clamp(held * gain, -ceiling, ceiling);
            ++sampleTime;
        }
    }

    int latencyInSamples() const override { return static_cast<int>(window) - 1; }

private:
    double sampleRate = 44100.0;
    int ceilingIndex = -1, releaseIndex = -1;

    std::size_t window = 2;
    std::vector<float> delayed, average, minValue;
    std::vector<std::int64_t> minIndex;
    double sum = 0.0;
    std::size_t position = 0, front = 0, count = 0;
    std::int64_t sampleTime = 0;
    float gain = 1.0f;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerDynamics(ElementRegistry& registry)
{
    registry.add("Limiter",          &make<elements::Limiter>);
    registry.add("EnvelopeFollower", &make<elements::EnvelopeFollower>);
    registry.add("Expander",         &make<elements::Expander>);
    registry.add("Compressor",       &make<elements::Compressor>);
    registry.add("Envelope",         &make<elements::Envelope>);
}
}  // namespace valis

// src/dsp/elements/Dynamics.cpp
//
// Detector and gain computer are separate elements, so a sidechain is a visible
// arc rather than a coupling hidden inside a compressor. Scream gates its
// feedback path from an envelope of the input, which only works that way.
//
// Compression curves follow Giannoulis, Massberg and Reiss (JAES 2012).

#include "Common.h"

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
    }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const float threshold = controlAt(args, thresholdIndex, -6.0f);
        const float ratio     = std::max(controlAt(args, ratioIndex, 4.0f), 1.0f);
        const float knee      = std::max(controlAt(args, kneeIndex, 6.0f), 0.0f);
        const float attack    = timeToCoeff(controlAt(args, attackIndex, 5.0f), sampleRate);
        const float release   = timeToCoeff(controlAt(args, releaseIndex, 100.0f), sampleRate);
        const float upward    = std::clamp(controlAt(args, upwardIndex, 0.0f), 0.0f, 1.0f);

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

            // Upward: lift what sits below the threshold.
            if (upward > 0.0f && levelDb < threshold)
                targetDb += upward * (threshold - levelDb) * (1.0f - ratioInv);

            out[i] = in[i] * dbToGain(targetDb - levelDb);
        }
    }

private:
    float envelope = 0.0f;
    int thresholdIndex = -1, ratioIndex = -1, kneeIndex = -1;
    int attackIndex = -1, releaseIndex = -1, upwardIndex = -1;
};

/// ADSR contour on a control output. Free-running for now: gate handling
/// arrives with MIDI in a later milestone.
/// TODO: drive the gate from note events rather than retriggering on prepare.
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
        reset();
    }

    void reset() override { level = 0.0f; }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 1)
            return;

        const float attack  = timeToCoeff(controlAt(args, attackIndex, 10.0f), sampleRate);
        const float sustain = std::clamp(controlAt(args, sustainIndex, 0.7f), 0.0f, 1.0f);

        // Rise toward sustain; the decay and release stages need note events.
        const float blockCoeff = std::pow(attack, static_cast<float>(args.numSamples));
        level = sustain + blockCoeff * (level - sustain);

        args.controlOut[0] = level;
    }

private:
    double sampleRate = 44100.0;
    float level = 0.0f;
    int attackIndex = -1, decayIndex = -1, sustainIndex = -1, releaseIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerDynamics(ElementRegistry& registry)
{
    registry.add("EnvelopeFollower", &make<elements::EnvelopeFollower>);
    registry.add("Expander",         &make<elements::Expander>);
    registry.add("Compressor",       &make<elements::Compressor>);
    registry.add("Envelope",         &make<elements::Envelope>);
}
}  // namespace valis

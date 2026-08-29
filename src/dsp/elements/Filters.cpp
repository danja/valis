// src/dsp/elements/Filters.cpp

#include "Common.h"

namespace valis::elements {

/// Topology-preserving transform state variable filter in Andy Simper's
/// "SvfLinearTrapOptimised2" form. All three responses come out of the same
/// state, which is why the ontology gives it lp, bp and hp as separate ports:
/// Scream taps one instance at lp and another at hp.
class StateVariable final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        cutoffIndex    = controlIndex(type, "cutoff");
        resonanceIndex = controlIndex(type, "resonance");
        lpOut = audioOutIndex(type, "lp");
        bpOut = audioOutIndex(type, "bp");
        hpOut = audioOutIndex(type, "hp");
        reset();
    }

    void reset() override { ic1eq = ic2eq = 0.0f; }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1)
            return;

        const float cutoff = std::clamp(controlAt(args, cutoffIndex, 1000.0f),
                                        20.0f, static_cast<float>(sampleRate * 0.45));
        const float q      = std::max(controlAt(args, resonanceIndex, 0.7071f), 0.05f);

        const float g  = static_cast<float>(std::tan(3.14159265358979 * cutoff / sampleRate));
        const float k  = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;

        const float* in = args.audioIn[0];
        float* lp = lpOut >= 0 && lpOut < args.numAudioOut ? args.audioOut[lpOut] : nullptr;
        float* bp = bpOut >= 0 && bpOut < args.numAudioOut ? args.audioOut[bpOut] : nullptr;
        float* hp = hpOut >= 0 && hpOut < args.numAudioOut ? args.audioOut[hpOut] : nullptr;

        for (int i = 0; i < args.numSamples; ++i)
        {
            const float v0 = in[i];
            const float v3 = v0 - ic2eq;
            const float v1 = a1 * ic1eq + a2 * v3;
            const float v2 = ic2eq + a2 * ic1eq + a3 * v3;

            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;

            if (lp != nullptr) lp[i] = v2;
            if (bp != nullptr) bp[i] = v1;
            if (hp != nullptr) hp[i] = v0 - k * v1 - v2;
        }
    }

private:
    double sampleRate = 44100.0;
    float ic1eq = 0.0f, ic2eq = 0.0f;
    int cutoffIndex = -1, resonanceIndex = -1;
    int lpOut = -1, bpOut = -1, hpOut = -1;
};

/// First-order TPT filter. mode selects lowpass, highpass or allpass.
class OnePole final : public MonoElement
{
public:
    void reset() override { state = 0.0f; }

protected:
    void cacheIndices(const ElementType& type) override
    {
        cutoffIndex = controlIndex(type, "cutoff");
        modeIndex   = controlIndex(type, "mode");
    }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const float cutoff = std::clamp(controlAt(args, cutoffIndex, 1000.0f),
                                        20.0f, static_cast<float>(sampleRate * 0.45));
        const int mode = static_cast<int>(controlAt(args, modeIndex, 0.0f) + 0.5f);

        const float g = static_cast<float>(std::tan(3.14159265358979 * cutoff / sampleRate));
        const float a = g / (1.0f + g);

        for (int i = 0; i < n; ++i)
        {
            const float v  = (in[i] - state) * a;
            const float lp = v + state;
            state = lp + v;

            switch (mode)
            {
                case 1:  out[i] = in[i] - lp;          break;   // highpass
                case 2:  out[i] = 2.0f * lp - in[i];   break;   // allpass
                default: out[i] = lp;                  break;   // lowpass
            }
        }
    }

private:
    float state = 0.0f;
    int cutoffIndex = -1, modeIndex = -1;
};

/// Four-pole Moog-style ladder with a saturating stage. Has memory and is
/// nonlinear, which is precisely why the taxonomy splits on memory rather than
/// on linearity.
class Ladder final : public MonoElement
{
public:
    void reset() override
    {
        for (auto& s : stage) s = 0.0f;
        for (auto& s : delay) s = 0.0f;
    }

protected:
    void cacheIndices(const ElementType& type) override
    {
        cutoffIndex    = controlIndex(type, "cutoff");
        resonanceIndex = controlIndex(type, "resonance");
        driveIndex     = controlIndex(type, "drive");
    }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const float cutoff = std::clamp(controlAt(args, cutoffIndex, 1000.0f),
                                        20.0f, static_cast<float>(sampleRate * 0.45));
        const float res    = std::clamp(controlAt(args, resonanceIndex, 0.0f), 0.0f, 1.0f);
        const float drive  = std::max(controlAt(args, driveIndex, 1.0f), 1.0f);

        const float wc = static_cast<float>(2.0 * 3.14159265358979 * cutoff / sampleRate);
        const float g  = std::clamp(wc * 0.5f, 0.0f, 0.95f);
        const float k  = res * 4.0f;

        for (int i = 0; i < n; ++i)
        {
            // Feedback around the four stages, saturated to keep it bounded.
            float x = in[i] * drive - k * delay[3];
            x = std::tanh(x);

            for (int s = 0; s < 4; ++s)
            {
                stage[s] += g * (x - delay[s]);
                delay[s]  = stage[s];
                x         = stage[s];
            }

            out[i] = x;
        }
    }

private:
    float stage[4] = {}, delay[4] = {};
    int cutoffIndex = -1, resonanceIndex = -1, driveIndex = -1;
};

/// One sample of delay. The only thing that may break a feedback loop, which is
/// why the compiler cuts its outgoing edge before looking for cycles.
class UnitDelay final : public MonoElement
{
public:
    void reset() override { previous = 0.0f; }
    int latencyInSamples() const override { return 1; }

protected:
    void processMono(const float* in, float* out, int n, const ProcessArgs&) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            out[i]   = previous;
            previous = in[i];
        }
    }

private:
    float previous = 0.0f;
};

/// Feedback comb filter with a one-pole lowpass in the feedback path — the
/// Karplus-Strong plucked-string algorithm.
///
/// The delay length is set by frequency: delayN = sampleRate / frequency.
/// The lowpass coefficient `damping` controls how fast high frequencies decay:
/// 0 = flat feedback (bright, long sustain); values near 1 damp highs quickly
/// (dark, muted). feedback controls overall decay rate (how loud each loop is).
class CombFilter final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        freqIndex     = controlIndex(type, "frequency");
        feedbackIndex = controlIndex(type, "feedback");
        dampingIndex  = controlIndex(type, "damping");
        // Allocate for minimum 10 Hz (sampleRate / 10 samples).
        buffer.assign(static_cast<std::size_t>(rate / 10.0) + 2, 0.0f);
        writePos    = 0;
        filterState = 0.0f;
    }

    void reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos    = 0;
        filterState = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float freq     = std::clamp(controlAt(args, freqIndex,    220.0f),
                                          10.0f, static_cast<float>(sampleRate * 0.45));
        const float feedback = std::clamp(controlAt(args, feedbackIndex, 0.95f), 0.0f, 0.99f);
        const float damping  = std::clamp(controlAt(args, dampingIndex,   0.1f), 0.0f, 1.0f);

        const int delayN  = std::max(1, static_cast<int>(sampleRate / freq + 0.5));
        const int bufSize = static_cast<int>(buffer.size());

        // One-pole LP coefficient: 0 = flat (bright), 0.95 = heavily smoothed (dark).
        const float a = damping * 0.95f;

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            int readPos = writePos - delayN;
            if (readPos < 0)
                readPos += bufSize;

            const float delayed = buffer[static_cast<std::size_t>(readPos)];

            // One-pole lowpass in the feedback path.
            filterState = a * filterState + (1.0f - a) * delayed;

            buffer[static_cast<std::size_t>(writePos)] = in[i] + filterState * feedback;
            out[i] = delayed;

            if (++writePos >= bufSize)
                writePos = 0;
        }
    }

private:
    std::vector<float> buffer;
    int writePos = 0;
    float filterState = 0.0f;
    double sampleRate = 44100.0;
    int freqIndex = -1, feedbackIndex = -1, dampingIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerFilters(ElementRegistry& registry)
{
    registry.add("StateVariable", &make<elements::StateVariable>);
    registry.add("OnePole",       &make<elements::OnePole>);
    registry.add("Ladder",        &make<elements::Ladder>);
    registry.add("UnitDelay",     &make<elements::UnitDelay>);
    registry.add("CombFilter",    &make<elements::CombFilter>);
}
}  // namespace valis

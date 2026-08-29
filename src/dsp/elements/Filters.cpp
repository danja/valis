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

/// Stiff string (beam) resonator — a CombFilter with allpass-chain dispersion
/// in the feedback path. The allpass stages shift phase non-uniformly, so
/// partials are stretched upward relative to exact harmonics, giving the
/// characteristic "inharmonic" sound of a piano string or metal bar.
///
/// dispersion controls the allpass coefficient: 0 = no stretching (pure comb),
/// 1 = heavy stretching (bell-like, strongly inharmonic). Four allpass stages
/// are used; more stages increase the dispersion without changing the formula.
///
/// Implementation: H_ap(z) = (a + z^-1) / (1 + a*z^-1), iterated 4 times in
/// the comb feedback path. Per sample: y = -a*x + s; s = x + a*y.
class StiffString final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        freqIndex       = controlIndex(type, "frequency");
        feedbackIndex   = controlIndex(type, "feedback");
        dampingIndex    = controlIndex(type, "damping");
        dispersionIndex = controlIndex(type, "dispersion");
        buffer.assign(static_cast<std::size_t>(rate / 10.0) + 2, 0.0f);
        writePos    = 0;
        filterState = 0.0f;
        for (auto& s : apState) s = 0.0f;
    }

    void reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos    = 0;
        filterState = 0.0f;
        for (auto& s : apState) s = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float freq       = std::clamp(controlAt(args, freqIndex,       220.0f),
                                            10.0f, static_cast<float>(sampleRate * 0.45));
        const float feedback   = std::clamp(controlAt(args, feedbackIndex,    0.95f), 0.0f, 0.99f);
        const float damping    = std::clamp(controlAt(args, dampingIndex,      0.1f), 0.0f, 1.0f);
        const float dispersion = std::clamp(controlAt(args, dispersionIndex,   0.1f), 0.0f, 1.0f);

        const int delayN  = std::max(1, static_cast<int>(sampleRate / freq + 0.5));
        const int bufSize = static_cast<int>(buffer.size());

        const float a  = damping * 0.95f;       // LP coefficient
        const float ap = dispersion * 0.85f;    // allpass coefficient

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            int readPos = writePos - delayN;
            if (readPos < 0)
                readPos += bufSize;

            const float delayed = buffer[static_cast<std::size_t>(readPos)];

            // One-pole LP (brightness decay).
            filterState = a * filterState + (1.0f - a) * delayed;

            // Four allpass stages in the feedback path (dispersion).
            float v = filterState;
            for (int s = 0; s < kApStages; ++s)
            {
                const float y = -ap * v + apState[s];
                apState[s]    = v + ap * y;
                v             = y;
            }

            buffer[static_cast<std::size_t>(writePos)] = in[i] + v * feedback;
            out[i] = delayed;

            if (++writePos >= bufSize)
                writePos = 0;
        }
    }

private:
    static constexpr int kApStages = 4;
    std::vector<float> buffer;
    float apState[kApStages] = {};
    int writePos    = 0;
    float filterState = 0.0f;
    double sampleRate = 44100.0;
    int freqIndex = -1, feedbackIndex = -1, dampingIndex = -1, dispersionIndex = -1;
};

/// Six parallel 2-pole resonators tuned to preset modal frequency ratios.
///
/// Each resonator is a lossless digital resonator (two-pole bandpass) with
/// pole radius set from a per-mode T60 decay time. Higher modes decay faster
/// by dividing the fundamental decay by their frequency ratio. The mode
/// parameter selects which physical object's ratios are used.
class ModalBank final : public DspElement
{
public:
    static constexpr int kModes      = 4;
    static constexpr int kResonators = 6;

    // Frequency ratios relative to the fundamental for each mode.
    static constexpr float kRatios[kModes][kResonators] = {
        // Marimba / free-free bar (Euler-Bernoulli beam, free BCs)
        { 1.0f, 2.756f, 5.404f, 8.933f, 13.344f, 18.648f },
        // Drumhead — circular membrane, Bessel zeros (01, 11, 21, 02, 31, 12)
        { 1.0f, 1.593f, 2.136f, 2.296f,  2.653f,  2.917f },
        // Rectangular membrane (1,1)(1,2)(2,1)(2,2)(1,3)(3,1) sqrt-of-sum-of-squares
        { 1.0f, 1.414f, 1.581f, 2.000f,  2.236f,  2.550f },
        // Plate — clamped rectangular, approximate
        { 1.0f, 1.414f, 2.000f, 2.236f,  2.449f,  2.828f },
    };

    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        freqIdx       = controlIndex(type, "frequency");
        decayIdx      = controlIndex(type, "decay");
        brightnessIdx = controlIndex(type, "brightness");
        modeIdx       = controlIndex(type, "mode");
        reset();
    }

    void reset() override
    {
        for (auto& r : res)
            r.y1 = r.y2 = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float baseFreq   = std::clamp(controlAt(args, freqIdx,       220.0f), 20.0f,
                                             static_cast<float>(sampleRate * 0.45));
        const float decayT60   = std::clamp(controlAt(args, decayIdx,        1.0f), 0.05f, 10.0f);
        const float brightness = std::clamp(controlAt(args, brightnessIdx,   0.5f), 0.0f, 1.0f);
        const int   mode       = std::clamp(static_cast<int>(
                                     controlAt(args, modeIdx, 0.0f) + 0.5f), 0, kModes - 1);

        const float* ratios = kRatios[mode];
        const float* in     = args.audioIn[0];
        float*       out    = args.audioOut[0];

        const float nyquist = static_cast<float>(sampleRate * 0.45);
        const float twoPi   = 6.28318530717959f;

        // Clear the output; each resonator accumulates into it.
        for (int i = 0; i < args.numSamples; ++i)
            out[i] = 0.0f;

        for (int n = 0; n < kResonators; ++n)
        {
            const float fn = std::min(baseFreq * ratios[n], nyquist);

            // Higher modes decay faster proportional to their ratio.
            const float T60n = decayT60 / ratios[n];
            const float r    = std::pow(0.001f, 1.0f / (static_cast<float>(sampleRate) * T60n));

            const float omega = twoPi * fn / static_cast<float>(sampleRate);
            const float coeff = 2.0f * r * std::cos(omega);
            const float r2    = r * r;

            // Spectral tilt: higher modes quieter when brightness is low.
            const float gain  = std::exp(-static_cast<float>(n) * (1.0f - brightness) * 2.0f);

            float y1 = res[n].y1;
            float y2 = res[n].y2;

            for (int i = 0; i < args.numSamples; ++i)
            {
                const float y = coeff * y1 - r2 * y2 + gain * in[i];
                y2 = y1;
                y1 = y;
                out[i] += y;
            }

            res[n].y1 = y1;
            res[n].y2 = y2;
        }

        // Six resonators summing to 1 each would clip; normalise.
        for (int i = 0; i < args.numSamples; ++i)
            out[i] *= 0.15f;
    }

private:
    struct Resonator { float y1 = 0.0f, y2 = 0.0f; };
    Resonator res[kResonators];
    double sampleRate = 44100.0;
    int freqIdx = -1, decayIdx = -1, brightnessIdx = -1, modeIdx = -1;
};

// constexpr static member definitions (C++17 inline, but explicit for C++14 compat)
constexpr float ModalBank::kRatios[ModalBank::kModes][ModalBank::kResonators];

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
    registry.add("StiffString",   &make<elements::StiffString>);
    registry.add("ModalBank",     &make<elements::ModalBank>);
}
}  // namespace valis

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
        driveIndex     = controlIndex(type, "drive");
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
        const float drive  = std::max(controlAt(args, driveIndex, 1.0f), 1.0f);

        const float g  = static_cast<float>(std::tan(3.14159265358979 * cutoff / sampleRate));
        const float k  = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;

        const float* in = args.audioIn[0];
        float* lp = lpOut >= 0 && lpOut < args.numAudioOut ? args.audioOut[lpOut] : nullptr;
        float* bp = bpOut >= 0 && bpOut < args.numAudioOut ? args.audioOut[bpOut] : nullptr;
        float* hp = hpOut >= 0 && hpOut < args.numAudioOut ? args.audioOut[hpOut] : nullptr;

        for (int i = 0; i < args.numSamples; ++i)
        {
            const float v0 = in[i];
            const float v3 = v0 - ic2eq;
            float v1 = a1 * ic1eq + a2 * v3;

            // Saturating the bandpass, which both integrators and the resonant feedback
            // are driven from, bounds the resonance instead of the filter's output: a
            // loud signal compresses and the filter can self-oscillate without growing.
            // At unity drive the shape is not applied and the filter is exactly linear.
            if (drive > 1.0f)
                v1 = std::tanh(drive * v1) / drive;

            const float v2 = ic2eq + g * v1;

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
    int cutoffIndex = -1, resonanceIndex = -1, driveIndex = -1;
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
///
/// Closing a loop it delays by one more control slice than that, because the
/// elements run a slice at a time: see tests/engine/UnitDelayLoopTest.cpp.
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
            const float tilt  = std::exp(-static_cast<float>(n) * (1.0f - brightness) * 2.0f);

            // Scale input by (1-r) so the resonator peak amplitude is independent
            // of T60: without this, a long decay builds up to ~1/(1-r) times the
            // input, which overflows the DAW's headroom at T60 > 500 ms.
            const float gain  = tilt * (1.0f - r);

            float y1 = res[n].y1;
            float y2 = res[n].y2;

            for (int i = 0; i < args.numSamples; ++i)
            {
                const float y = coeff * y1 - r2 * y2 + gain * in[i];
                y2 = y1;
                y1 = y;
                out[i] += y;
            }

            res[n].y1 = std::isfinite(y1) ? y1 : 0.0f;
            res[n].y2 = std::isfinite(y2) ? y2 : 0.0f;
        }

        // Resonator peak with (1-r) excitation ≈ tilt * noise_rms * sqrt(excitation_samples / 2).
        // For 8 ms burst at 44.1 kHz that is roughly 0.015 per mode; 6 modes sum to ≈ 0.04.
        // The 15× boost brings the output into a comfortable range without relying on T60.
        for (int i = 0; i < args.numSamples; ++i)
            out[i] *= 15.0f;
    }

private:
    struct Resonator { float y1 = 0.0f, y2 = 0.0f; };
    Resonator res[kResonators];
    double sampleRate = 44100.0;
    int freqIdx = -1, decayIdx = -1, brightnessIdx = -1, modeIdx = -1;
};


/// Classic bucket-brigade chorus with three LFO-modulated delay lines.
///
/// Three voices share a single circular buffer with evenly distributed LFO
/// phases (0°, 120°, 240°). Modulation is at block rate (kControlBlock samples);
/// the LFO frequency is far below audio rate so block-rate updates produce
/// smooth sweeps. Buffer sized to 40 ms at prepare time; process() never
/// allocates.
class Chorus final : public DspElement
{
    static constexpr float  kCenterMs   = 7.0f;
    static constexpr float  kMaxDepthMs = 3.5f;
    static constexpr double kBufMs      = (kCenterMs + kMaxDepthMs) * 2.0;
    static constexpr int    kVoices     = 3;

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        rateIndex  = controlIndex(type, "rate");
        depthIndex = controlIndex(type, "depth");
        mixIndex   = controlIndex(type, "mix");

        const int len = static_cast<int>(rate * kBufMs * 0.001) + 4;
        buffer.assign(static_cast<std::size_t>(len), 0.0f);
        writePos = 0;
        for (int v = 0; v < kVoices; ++v)
            phase[v] = static_cast<float>(v) / static_cast<float>(kVoices);
    }

    void reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float rate    = std::clamp(controlAt(args, rateIndex,  0.5f), 0.01f, 10.0f);
        const float depth   = std::clamp(controlAt(args, depthIndex, 2.0f), 0.0f, kMaxDepthMs);
        const float mix     = std::clamp(controlAt(args, mixIndex,   0.5f), 0.0f, 1.0f);

        const float centerSamples = static_cast<float>(sampleRate) * kCenterMs * 0.001f;
        const float depthSamples  = static_cast<float>(sampleRate) * depth * 0.001f;
        const float phaseInc      = rate / static_cast<float>(sampleRate);
        const int   bufLen        = static_cast<int>(buffer.size());

        // Compute per-voice delay offsets from block-rate LFO values.
        float delayOffset[kVoices];
        for (int v = 0; v < kVoices; ++v)
        {
            const float lfo = std::sin(phase[v] * 6.28318530718f);
            delayOffset[v]  = centerSamples + lfo * depthSamples;
        }

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            buffer[static_cast<std::size_t>(writePos)] = in[i];

            float wet = 0.0f;
            for (int v = 0; v < kVoices; ++v)
            {
                const float d     = delayOffset[v];
                const int   iD    = static_cast<int>(d);
                const float frac  = d - static_cast<float>(iD);
                const int   r0    = ((writePos - iD)     % bufLen + bufLen) % bufLen;
                const int   r1    = ((writePos - iD - 1) % bufLen + bufLen) % bufLen;
                wet += buffer[static_cast<std::size_t>(r0)] * (1.0f - frac)
                     + buffer[static_cast<std::size_t>(r1)] * frac;
            }
            wet /= static_cast<float>(kVoices);
            out[i] = in[i] * (1.0f - mix) + wet * mix;

            if (++writePos >= bufLen)
                writePos = 0;
        }

        // Advance LFO phases at block rate.
        for (int v = 0; v < kVoices; ++v)
        {
            phase[v] += phaseInc * static_cast<float>(args.numSamples);
            while (phase[v] >= 1.0f) phase[v] -= 1.0f;
        }
    }

private:
    std::vector<float> buffer;
    int   writePos      = 0;
    float phase[kVoices]{};
    double sampleRate   = 44100.0;
    int rateIndex = -1, depthIndex = -1, mixIndex = -1;
};

/// Single-voice LFO-modulated short delay with feedback. The comb-filtering
/// from the feedback creates the characteristic jet-flanging sweep. Center
/// delay is 5 ms; depth sweeps up to ±4 ms around that centre. Buffer sized
/// at prepare time; process() never allocates.
class Flanger final : public DspElement
{
    static constexpr float  kCenterMs   = 5.0f;
    static constexpr float  kMaxDepthMs = 4.0f;
    static constexpr double kBufMs      = (kCenterMs + kMaxDepthMs) * 2.0;
    static constexpr float  kMaxFb      = 0.95f;

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        rateIndex  = controlIndex(type, "rate");
        depthIndex = controlIndex(type, "depth");
        fbIndex    = controlIndex(type, "feedback");
        mixIndex   = controlIndex(type, "mix");

        const int len = static_cast<int>(rate * kBufMs * 0.001) + 4;
        buffer.assign(static_cast<std::size_t>(len), 0.0f);
        writePos      = 0;
        phase         = 0.0f;
        feedbackState = 0.0f;
    }

    void reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos      = 0;
        feedbackState = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float rate  = std::clamp(controlAt(args, rateIndex,  0.5f), 0.01f, 10.0f);
        const float depth = std::clamp(controlAt(args, depthIndex, 2.0f), 0.0f, kMaxDepthMs);
        const float fb    = std::clamp(controlAt(args, fbIndex,    0.5f), -kMaxFb, kMaxFb);
        const float mix   = std::clamp(controlAt(args, mixIndex,   0.5f), 0.0f, 1.0f);

        const float centerSamples = static_cast<float>(sampleRate) * kCenterMs * 0.001f;
        const float depthSamples  = static_cast<float>(sampleRate) * depth * 0.001f;
        const float phaseInc      = rate / static_cast<float>(sampleRate);
        const int   bufLen        = static_cast<int>(buffer.size());

        const float lfo = std::sin(phase * 6.28318530718f);
        const float d   = centerSamples + lfo * depthSamples;
        const int   iD  = static_cast<int>(d);
        const float frac = d - static_cast<float>(iD);

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];
        float        fbSample = feedbackState;

        for (int i = 0; i < args.numSamples; ++i)
        {
            buffer[static_cast<std::size_t>(writePos)] = in[i] + fb * fbSample;

            const int r0 = ((writePos - iD)     % bufLen + bufLen) % bufLen;
            const int r1 = ((writePos - iD - 1) % bufLen + bufLen) % bufLen;
            const float wet = buffer[static_cast<std::size_t>(r0)] * (1.0f - frac)
                            + buffer[static_cast<std::size_t>(r1)] * frac;
            fbSample = wet;
            out[i] = in[i] * (1.0f - mix) + wet * mix;

            if (++writePos >= bufLen) writePos = 0;
        }
        feedbackState = fbSample;

        phase += phaseInc * static_cast<float>(args.numSamples);
        while (phase >= 1.0f) phase -= 1.0f;
    }

private:
    std::vector<float> buffer;
    int    writePos       = 0;
    float  phase          = 0.0f;
    float  feedbackState  = 0.0f;
    double sampleRate     = 44100.0;
    int rateIndex = -1, depthIndex = -1, fbIndex = -1, mixIndex = -1;
};


/// Four-stage first-order all-pass chain with a block-rate sine LFO sweeping
/// the pole frequency. Feedback from the chain output back to the input
/// deepens the notches. The all-pass transfer function per stage is
///   H(z) = (g + z⁻¹) / (1 + g·z⁻¹)
/// where g = (tan(π·fc/fs) − 1) / (tan(π·fc/fs) + 1) and fc is swept between
/// 100 Hz and 100 + depth·1900 Hz.
class Phaser final : public DspElement
{
    static constexpr int kStages = 4;

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        rateIndex  = controlIndex(type, "rate");
        depthIndex = controlIndex(type, "depth");
        fbIndex    = controlIndex(type, "feedback");
        mixIndex   = controlIndex(type, "mix");

        std::fill(xm1, xm1 + kStages, 0.0f);
        std::fill(ym1, ym1 + kStages, 0.0f);
        phase   = 0.0f;
        fbState = 0.0f;
    }

    void reset() override
    {
        std::fill(xm1, xm1 + kStages, 0.0f);
        std::fill(ym1, ym1 + kStages, 0.0f);
        fbState = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float rate  = std::clamp(controlAt(args, rateIndex,  0.5f), 0.01f, 10.0f);
        const float depth = std::clamp(controlAt(args, depthIndex, 0.7f), 0.0f, 1.0f);
        const float fb    = std::clamp(controlAt(args, fbIndex,    0.5f), 0.0f, 0.9f);
        const float mix   = std::clamp(controlAt(args, mixIndex,   0.5f), 0.0f, 1.0f);

        const float lfo = std::sin(phase * 6.28318530718f);
        phase += rate / static_cast<float>(sampleRate) * static_cast<float>(args.numSamples);
        while (phase >= 1.0f) phase -= 1.0f;

        const float fcMin = 100.0f;
        const float fc    = fcMin + depth * 1900.0f * (0.5f + 0.5f * lfo);
        const float w     = std::tan(3.14159265358979f * fc / static_cast<float>(sampleRate));
        const float g     = (w - 1.0f) / (w + 1.0f);

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            float x = in[i] + fb * fbState;
            for (int s = 0; s < kStages; ++s)
            {
                const float y = g * x + xm1[s] - g * ym1[s];
                xm1[s] = x;
                ym1[s] = y;
                x = y;
            }
            fbState = x;
            out[i] = in[i] * (1.0f - mix) + x * mix;
        }
    }

private:
    float  xm1[kStages]{};
    float  ym1[kStages]{};
    float  phase      = 0.0f;
    float  fbState    = 0.0f;
    double sampleRate = 44100.0;
    int rateIndex = -1, depthIndex = -1, fbIndex = -1, mixIndex = -1;
};

/// Second-order section with the response chosen by a selector: the shelves, bell and
/// notch of an equaliser, and 12 dB per octave low and high cut. Coefficients are the
/// RBJ cookbook's (https://www.w3.org/TR/audio-eq-cookbook/). Held in double
/// precision, because a low shelf at 20 Hz puts the poles close to the unit circle and
/// single precision rounds them onto it.
class Biquad final : public MonoElement
{
    enum Mode { lowShelf, bell, highShelf, notch, lowCut, highCut };

public:
    void reset() override { z1 = z2 = 0.0; }

protected:
    void cacheIndices(const ElementType& type) override
    {
        modeIndex      = controlIndex(type, "mode");
        frequencyIndex = controlIndex(type, "frequency");
        gainIndex      = controlIndex(type, "gain");
        qIndex         = controlIndex(type, "q");
    }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const int    mode = static_cast<int>(std::lround(controlAt(args, modeIndex, 1.0f)));
        const double freq = std::clamp(static_cast<double>(controlAt(args, frequencyIndex, 1000.0f)),
                                       20.0, sampleRate * 0.45);
        const double gain = std::clamp(static_cast<double>(controlAt(args, gainIndex, 0.0f)), -24.0, 24.0);
        const double q    = std::clamp(static_cast<double>(controlAt(args, qIndex, 0.7071f)), 0.1, 20.0);

        const double w0    = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double cs    = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double A     = std::pow(10.0, gain / 40.0);
        const double beta  = 2.0 * std::sqrt(A) * alpha;

        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
        switch (mode)
        {
            case lowShelf:
                b0 = A * ((A + 1) - (A - 1) * cs + beta);
                b1 = 2 * A * ((A - 1) - (A + 1) * cs);
                b2 = A * ((A + 1) - (A - 1) * cs - beta);
                a0 = (A + 1) + (A - 1) * cs + beta;
                a1 = -2 * ((A - 1) + (A + 1) * cs);
                a2 = (A + 1) + (A - 1) * cs - beta;
                break;
            case highShelf:
                b0 = A * ((A + 1) + (A - 1) * cs + beta);
                b1 = -2 * A * ((A - 1) + (A + 1) * cs);
                b2 = A * ((A + 1) + (A - 1) * cs - beta);
                a0 = (A + 1) - (A - 1) * cs + beta;
                a1 = 2 * ((A - 1) - (A + 1) * cs);
                a2 = (A + 1) - (A - 1) * cs - beta;
                break;
            case notch:
                b0 = 1; b1 = -2 * cs; b2 = 1;
                a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha;
                break;
            case lowCut:
                b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = b0;
                a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha;
                break;
            case highCut:
                b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = b0;
                a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha;
                break;
            case bell:
            default:
                b0 = 1 + alpha * A; b1 = -2 * cs; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cs; a2 = 1 - alpha / A;
                break;
        }

        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

        // Transposed direct form II.
        for (int i = 0; i < n; ++i)
        {
            const double x = in[i];
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            out[i] = static_cast<float>(y);
        }
    }

private:
    double z1 = 0.0, z2 = 0.0;
    int modeIndex = -1, frequencyIndex = -1, gainIndex = -1, qIndex = -1;
};

/// Linkwitz-Riley 4th-order crossover (Linkwitz and Riley, JAES 1976): two Butterworth
/// biquads in series per side. The two sides are 6 dB down at the corner and in phase
/// with each other, so low + high is flat in magnitude at every frequency, and a band
/// processed apart and summed again comes back unchanged.
class Crossover final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate     = rate;
        frequencyIndex = controlIndex(type, "frequency");
        lowOut         = audioOutIndex(type, "low");
        highOut        = audioOutIndex(type, "high");
        reset();
    }

    void reset() override
    {
        for (auto& stage : lowStage)  stage = {};
        for (auto& stage : highStage) stage = {};
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1)
            return;

        const double freq = std::clamp(static_cast<double>(controlAt(args, frequencyIndex, 1000.0f)),
                                       20.0, sampleRate * 0.45);
        const double w0    = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double cs    = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * 0.70710678118654752);
        const double a0    = 1.0 + alpha;
        const double a1    = -2.0 * cs / a0;
        const double a2    = (1.0 - alpha) / a0;
        const double lb0   = (1.0 - cs) / 2.0 / a0;
        const double lb1   = (1.0 - cs) / a0;
        const double hb0   = (1.0 + cs) / 2.0 / a0;
        const double hb1   = -(1.0 + cs) / a0;

        const float* in   = args.audioIn[0];
        float*       low  = lowOut  >= 0 && lowOut  < args.numAudioOut ? args.audioOut[lowOut]  : nullptr;
        float*       high = highOut >= 0 && highOut < args.numAudioOut ? args.audioOut[highOut] : nullptr;

        for (int i = 0; i < args.numSamples; ++i)
        {
            const double x = in[i];
            const double l = lowStage[1].run(lowStage[0].run(x, lb0, lb1, lb0, a1, a2), lb0, lb1, lb0, a1, a2);
            const double h = highStage[1].run(highStage[0].run(x, hb0, hb1, hb0, a1, a2), hb0, hb1, hb0, a1, a2);
            if (low != nullptr)  low[i]  = static_cast<float>(l);
            if (high != nullptr) high[i] = static_cast<float>(h);
        }
    }

private:
    struct Section
    {
        double z1 = 0.0, z2 = 0.0;

        double run(double x, double b0, double b1, double b2, double a1, double a2) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    double sampleRate = 44100.0;
    Section lowStage[2], highStage[2];
    int frequencyIndex = -1, lowOut = -1, highOut = -1;
};

/// Bit-depth and sample-rate reducer. The input is held for `downsample` samples and
/// rounded to `bits` bits, with nothing filtered either side, so the aliasing and the
/// rounding error are the sound. Jitter varies the length of each hold, drawn from a
/// generator that restarts at every reset, so a render is repeatable.
class Redux final : public MonoElement
{
public:
    void reset() override
    {
        held       = 0.0f;
        untilNext  = 0.0f;
        random     = 0x9E3779B9u;
    }

protected:
    void cacheIndices(const ElementType& type) override
    {
        bitsIndex   = controlIndex(type, "bits");
        factorIndex = controlIndex(type, "downsample");
        jitterIndex = controlIndex(type, "jitter");
    }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const float bits   = std::clamp(controlAt(args, bitsIndex, 8.0f), 1.0f, 24.0f);
        const float factor = std::clamp(controlAt(args, factorIndex, 4.0f), 1.0f, 64.0f);
        const float jitter = std::clamp(controlAt(args, jitterIndex, 0.0f), 0.0f, 1.0f);

        // A word of `bits` bits has 2^(bits-1) steps either side of zero. From 24 bits
        // up the float itself is the limit, so the input is left alone.
        const bool  rounds = bits < 24.0f;
        const float steps  = std::exp2(bits - 1.0f);

        for (int i = 0; i < n; ++i)
        {
            if (untilNext <= 0.0f)
            {
                held = rounds ? std::round(in[i] * steps) / steps : in[i];

                float length = factor;
                if (jitter > 0.0f)
                    length *= 1.0f + jitter * (nextRandom() - 0.5f);
                untilNext += std::max(length, 1.0f);
            }
            untilNext -= 1.0f;
            out[i] = held;
        }
    }

private:
    /// xorshift32 mapped to [0, 1).
    float nextRandom() noexcept
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return static_cast<float>(random >> 8) * (1.0f / 16777216.0f);
    }

    float held = 0.0f, untilNext = 0.0f;
    std::uint32_t random = 0x9E3779B9u;
    int bitsIndex = -1, factorIndex = -1, jitterIndex = -1;
};

/// Pitch shifter with two read heads sweeping through a delay line.
///
/// Each head's delay changes by (1 - ratio) samples per sample, so it plays the line
/// faster or slower than it was written: the pitch moves, as a tape would. A head can
/// only sweep so far before it runs out of line, so it jumps back, and a window hides
/// the jump. The two heads sit half a grain apart under complementary windows
/// (sin^2 and cos^2, which sum to one), so one is always at full level while the other
/// is jumping.
///
/// The dry signal is delayed by half a grain, which is the average delay of the shifted
/// one, so mixing the two does not put a comb in the response. That half grain is
/// declared as latency.
class PitchShift final : public DspElement
{
    static constexpr double kGrainMs = 30.0;

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        pitchIndex = controlIndex(type, "pitch");
        mixIndex   = controlIndex(type, "mix");

        grain = std::max(64, static_cast<int>(kGrainMs * 0.001 * rate));
        line.assign(static_cast<std::size_t>(grain) * 2 + 8, 0.0f);
        reset();
    }

    void reset() override
    {
        std::fill(line.begin(), line.end(), 0.0f);
        writePos = 0;
        // Head one starts at half a grain with the window fully open; head two at
        // zero with it closed. At a shift of zero this is a plain half-grain delay.
        phase = 0.5;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float semitones = std::clamp(controlAt(args, pitchIndex, 0.0f), -24.0f, 24.0f);
        const float mix       = std::clamp(controlAt(args, mixIndex, 1.0f), 0.0f, 1.0f);
        const double ratio    = std::exp2(static_cast<double>(semitones) / 12.0);

        // Per sample, the delay as a fraction of a grain moves by (1 - ratio) / grain.
        const double step = (1.0 - ratio) / static_cast<double>(grain);
        const int    size = static_cast<int>(line.size());

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            line[static_cast<std::size_t>(writePos)] = in[i];

            const double p1 = phase;
            const double p2 = phase < 0.5 ? phase + 0.5 : phase - 0.5;

            const float shifted = read(p1, size) * window(p1) + read(p2, size) * window(p2);
            const float dry     = read(0.5, size);

            out[i] = dry * (1.0f - mix) + shifted * mix;

            phase += step;
            if (phase >= 1.0) phase -= 1.0;
            if (phase < 0.0)  phase += 1.0;

            if (++writePos >= size)
                writePos = 0;
        }
    }

    int latencyInSamples() const override { return grain / 2; }

private:
    /// sin^2 of half a turn: 0 at the ends of the sweep, 1 in the middle.
    static float window(double p) noexcept
    {
        const auto s = static_cast<float>(std::sin(3.14159265358979323846 * p));
        return s * s;
    }

    /// The line `p` of a grain ago, linearly interpolated.
    float read(double p, int size) const noexcept
    {
        const double delay = p * grain;
        const int    whole = static_cast<int>(delay);
        const float  frac  = static_cast<float>(delay - whole);

        int a = writePos - whole;
        if (a < 0) a += size;
        int b = a - 1;
        if (b < 0) b += size;

        return line[static_cast<std::size_t>(a)] * (1.0f - frac) + line[static_cast<std::size_t>(b)] * frac;
    }

    double sampleRate = 44100.0;
    int grain = 1323;
    int writePos = 0;
    double phase = 0.5;
    std::vector<float> line;
    int pitchIndex = -1, mixIndex = -1;
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
    registry.add("StiffString",   &make<elements::StiffString>);
    registry.add("ModalBank",     &make<elements::ModalBank>);
    registry.add("Chorus",        &make<elements::Chorus>);
    registry.add("Flanger",       &make<elements::Flanger>);
    registry.add("Phaser",        &make<elements::Phaser>);
    registry.add("Biquad",        &make<elements::Biquad>);
    registry.add("Crossover",     &make<elements::Crossover>);
    registry.add("Redux",         &make<elements::Redux>);
    registry.add("PitchShift",    &make<elements::PitchShift>);
}
}  // namespace valis

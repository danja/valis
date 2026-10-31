// src/dsp/elements/Filters.cpp

#include "Common.h"

#include "Waveguide.h"
#include "dsp/Random.h"

#include <array>
#include <cctype>
#include <complex>
#include <cstdlib>
#include <string>
#include <vector>

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
///
/// The loop is tuned by its total delay, so the delay line is shortened by what
/// the damping filter and the allpass chain add at the fundamental, and read at
/// a fractional position (see Waveguide.h).
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
        line.prepare(static_cast<std::size_t>(rate / 10.0) + 4);
        reset();
    }

    void reset() override
    {
        line.clear();
        loss.clear();
        for (auto& s : apState) s = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float freq       = std::clamp(controlAt(args, freqIndex,       220.0f),
                                            10.0f, static_cast<float>(sampleRate * 0.45));
        const float feedback   = std::clamp(controlAt(args, feedbackIndex,    0.95f), 0.0f, 0.9999f);
        const float damping    = std::clamp(controlAt(args, dampingIndex,      0.1f), 0.0f, 1.0f);
        const float dispersion = std::clamp(controlAt(args, dispersionIndex,   0.1f), 0.0f, 1.0f);

        const float ap = dispersion * 0.85f;    // allpass coefficient
        loss.setPole(damping * 0.95f);

        const float omega = static_cast<float>(2.0 * M_PI * freq / sampleRate);
        line.setDelay(static_cast<float>(sampleRate / freq)
                      - loss.phaseDelay(omega) - allpassPhaseDelay(ap, omega) * kApStages);

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            const float delayed = line.last();

            // One-pole LP (brightness decay).
            float v = loss.process(delayed);

            // Four allpass stages in the feedback path (dispersion).
            for (int s = 0; s < kApStages; ++s)
            {
                const float y = -ap * v + apState[s];
                apState[s]    = v + ap * y;
                v             = y;
            }

            line.tick(in[i] + v * feedback);
            out[i] = delayed;
        }
    }

private:
    static constexpr int kApStages = 4;

    /// Phase delay of one allpass stage at omega, in samples.
    static float allpassPhaseDelay(float coefficient, float omega) noexcept
    {
        const std::complex<float> z1 = std::polar(1.0f, -omega);
        const std::complex<float> h  = (-coefficient + z1) / (1.0f - coefficient * z1);
        float lag = -std::arg(h);
        if (lag < -1.0e-6f)
            lag += 2.0f * static_cast<float>(M_PI);
        return lag / omega;
    }

    waveguide::Delay line;
    waveguide::Loss  loss;
    float apState[kApStages] = {};
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


/// A bank of damped resonators whose frequency, ring time and level are each set by the circuit, for a
/// sound that is measured and not a textbook shape: a bell, a cowbell, a conga fitted from a recording.
/// The modes come from the val:modes option, one "hertz:seconds:gain" triple apiece. Each resonator is
/// the impulse response gain * r^n * sin((n+1) w), so a unit impulse in plays back the numbers written.
class FreeModalBank final : public DspElement
{
public:
    static constexpr std::size_t kMaxModes = 64;

    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        tuneIdx    = controlIndex(type, "tune");
        decayIdx   = controlIndex(type, "decay");
        reset();
    }

    void reset() override
    {
        for (auto& m : modes)
            m.y1 = m.y2 = 0.0f;
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error) override
    {
        if (key != "modes")
            return true;

        std::vector<Mode> parsed;
        const std::string text(value);
        std::size_t at = 0;
        while (at < text.size())
        {
            while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
            const std::size_t start = at;
            while (at < text.size() && ! std::isspace(static_cast<unsigned char>(text[at]))) ++at;
            if (start == at)
                break;

            const std::string token = text.substr(start, at - start);
            double number[3] = { 0.0, 0.0, 1.0 };
            int found = 0;
            std::size_t from = 0;
            bool good = true;
            while (from <= token.size() && found < 4)
            {
                const auto colon = token.find(':', from);
                const std::string part = token.substr(from, colon == std::string::npos ? std::string::npos : colon - from);
                char* end = nullptr;
                const double v = std::strtod(part.c_str(), &end);
                if (part.empty() || end != part.c_str() + part.size() || ! std::isfinite(v) || found >= 3)
                {
                    good = false;
                    break;
                }
                number[found++] = v;
                if (colon == std::string::npos)
                    break;
                from = colon + 1;
            }

            if (! good || found < 2 || number[0] < 20.0 || number[0] > 20000.0
                || number[1] < 0.005 || number[1] > 60.0 || number[2] < 0.0 || number[2] > 100.0)
            {
                error = "cannot read the mode '" + token + "': expected hertz:seconds or hertz:seconds:gain, with "
                        "hertz 20 to 20000, seconds 0.005 to 60 (the time to fall 60 dB) and gain 0 to 100";
                return false;
            }
            if (parsed.size() >= kMaxModes)
            {
                error = "a bank has at most " + std::to_string(kMaxModes) + " modes";
                return false;
            }
            Mode m;
            m.hertz   = static_cast<float>(number[0]);
            m.seconds = static_cast<float>(number[1]);
            m.gain    = static_cast<float>(number[2]);
            parsed.push_back(m);
        }

        if (parsed.empty())
        {
            error = "val:modes needs at least one mode, written hertz:seconds:gain";
            return false;
        }

        count = parsed.size();
        for (std::size_t i = 0; i < kMaxModes; ++i)
            modes[i] = i < count ? parsed[i] : Mode{};
        return true;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float tune  = std::clamp(controlAt(args, tuneIdx,  1.0f), 0.25f, 4.0f);
        const float decay = std::clamp(controlAt(args, decayIdx, 1.0f), 0.1f, 10.0f);
        const float rate  = static_cast<float>(sampleRate);
        const float nyquist = rate * 0.45f;

        const float* in = args.audioIn[0];
        float* out = args.audioOut[0];
        for (int i = 0; i < args.numSamples; ++i)
            out[i] = 0.0f;

        for (std::size_t n = 0; n < count; ++n)
        {
            auto& m = modes[n];
            const float f = m.hertz * tune;
            if (f >= nyquist)
                continue;   // a tuned-up mode past the top of the band is dropped, not folded back

            const float omega = 6.28318530717959f * f / rate;
            const float r     = std::pow(0.001f, 1.0f / (rate * m.seconds * decay));
            const float coeff = 2.0f * r * std::cos(omega);
            const float r2    = r * r;
            const float b0    = m.gain * std::sin(omega);

            float y1 = m.y1, y2 = m.y2;
            for (int i = 0; i < args.numSamples; ++i)
            {
                const float y = coeff * y1 - r2 * y2 + b0 * in[i];
                y2 = y1;
                y1 = y;
                out[i] += y;
            }
            m.y1 = std::isfinite(y1) ? y1 : 0.0f;
            m.y2 = std::isfinite(y2) ? y2 : 0.0f;
        }
    }

private:
    struct Mode
    {
        float hertz = 0.0f, seconds = 1.0f, gain = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    std::array<Mode, kMaxModes> modes{};
    std::size_t count = 0;
    double sampleRate = 44100.0;
    int tuneIdx = -1, decayIdx = -1;
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
        reset();
    }

    void reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;

        // Evenly spaced round the cycle, and back there when the transport relocates.
        for (int v = 0; v < kVoices; ++v)
            phase[v] = static_cast<double>(v) / static_cast<double>(kVoices);
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
        const double phaseInc     = static_cast<double>(rate) / sampleRate;
        const int   bufLen        = static_cast<int>(buffer.size());

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            buffer[static_cast<std::size_t>(writePos)] = in[i];

            // The delay follows the LFO sample by sample. Held for a whole block it would step
            // at every block boundary, which a fast rate or a long block makes audible, and the
            // sound would depend on how the host cut the stream.
            float wet = 0.0f;
            for (int v = 0; v < kVoices; ++v)
            {
                const float lfo   = std::sin(static_cast<float>(phase[v] * 6.28318530718));
                const float d     = centerSamples + lfo * depthSamples;
                const int   iD    = static_cast<int>(d);
                const float frac  = d - static_cast<float>(iD);
                const int   r0    = ((writePos - iD)     % bufLen + bufLen) % bufLen;
                const int   r1    = ((writePos - iD - 1) % bufLen + bufLen) % bufLen;
                wet += buffer[static_cast<std::size_t>(r0)] * (1.0f - frac)
                     + buffer[static_cast<std::size_t>(r1)] * frac;

                phase[v] += phaseInc;
                if (phase[v] >= 1.0) phase[v] -= 1.0;
            }
            wet /= static_cast<float>(kVoices);
            out[i] = in[i] * (1.0f - mix) + wet * mix;

            if (++writePos >= bufLen)
                writePos = 0;
        }
    }

private:
    std::vector<float> buffer;
    int    writePos     = 0;
    double phase[kVoices]{};
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
        reset();
    }

    void reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos      = 0;
        phase         = 0.0;
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
        const double phaseInc     = static_cast<double>(rate) / sampleRate;
        const int   bufLen        = static_cast<int>(buffer.size());

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];
        float        fbSample = feedbackState;

        for (int i = 0; i < args.numSamples; ++i)
        {
            buffer[static_cast<std::size_t>(writePos)] = in[i] + fb * fbSample;

            // The delay follows the LFO sample by sample, so it neither steps at a block
            // boundary nor depends on where the host put one.
            const float lfo  = std::sin(static_cast<float>(phase * 6.28318530718));
            const float d    = centerSamples + lfo * depthSamples;
            const int   iD   = static_cast<int>(d);
            const float frac = d - static_cast<float>(iD);
            phase += phaseInc;
            if (phase >= 1.0) phase -= 1.0;

            const int r0 = ((writePos - iD)     % bufLen + bufLen) % bufLen;
            const int r1 = ((writePos - iD - 1) % bufLen + bufLen) % bufLen;
            const float wet = buffer[static_cast<std::size_t>(r0)] * (1.0f - frac)
                            + buffer[static_cast<std::size_t>(r1)] * frac;
            fbSample = wet;
            out[i] = in[i] * (1.0f - mix) + wet * mix;

            if (++writePos >= bufLen) writePos = 0;
        }
        feedbackState = fbSample;
    }

private:
    std::vector<float> buffer;
    int    writePos       = 0;
    double phase          = 0.0;
    float  feedbackState  = 0.0f;
    double sampleRate     = 44100.0;
    int rateIndex = -1, depthIndex = -1, fbIndex = -1, mixIndex = -1;
};


/// Four-stage first-order all-pass chain with a sine LFO sweeping
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

        reset();
    }

    void reset() override
    {
        std::fill(xm1, xm1 + kStages, 0.0f);
        std::fill(ym1, ym1 + kStages, 0.0f);
        phase   = 0.0;
        fbState = 0.0f;
        untilUpdate = 0;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float rate  = std::clamp(controlAt(args, rateIndex,  0.5f), 0.01f, 10.0f);
        const float depth = std::clamp(controlAt(args, depthIndex, 0.7f), 0.0f, 1.0f);
        const float fb    = std::clamp(controlAt(args, fbIndex,    0.5f), 0.0f, 0.9f);
        const float mix   = std::clamp(controlAt(args, mixIndex,   0.5f), 0.0f, 1.0f);

        const double phaseInc = static_cast<double>(rate) / sampleRate;
        constexpr float fcMin = 100.0f;

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            // The sweep follows the stream. The tangent is the costly part, so the pole is
            // recomputed every kUpdate samples, counted across blocks and not within them,
            // which keeps the sound the same however the host cuts the stream.
            if (untilUpdate <= 0)
            {
                const float lfo = std::sin(static_cast<float>(phase * 6.28318530718));
                const float fc  = fcMin + depth * 1900.0f * (0.5f + 0.5f * lfo);
                const float w   = std::tan(3.14159265358979f * fc / static_cast<float>(sampleRate));
                g = (w - 1.0f) / (w + 1.0f);
                untilUpdate = kUpdate;
            }
            --untilUpdate;
            phase += phaseInc;
            if (phase >= 1.0) phase -= 1.0;

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
    static constexpr int kUpdate = 8;

    float  xm1[kStages]{};
    float  ym1[kStages]{};
    double phase      = 0.0;
    float  g          = 0.0f;
    int    untilUpdate = 0;
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
        alignIndex = controlIndex(type, "align");

        grain = std::max(64, static_cast<int>(kGrainMs * 0.001 * rate));
        line.assign(static_cast<std::size_t>(grain) * 2 + 8, 0.0f);
        reset();
    }

    void reset() override
    {
        std::fill(line.begin(), line.end(), 0.0f);
        writePos = 0;
        offset[0] = offset[1] = 0;
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
        const bool   align = controlAt(args, alignIndex, 0.0f) > 0.5f;

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            line[static_cast<std::size_t>(writePos)] = in[i];

            const double p1 = phase;
            const double p2 = phase < 0.5 ? phase + 0.5 : phase - 0.5;

            const float shifted = read(p1, size, offset[0]) * window(p1) + read(p2, size, offset[1]) * window(p2);
            const float dry     = read(0.5, size, 0);

            out[i] = dry * (1.0f - mix) + shifted * mix;

            const double before = phase;
            phase += step;
            const bool wrapped = phase >= 1.0 || phase < 0.0;
            if (phase >= 1.0) phase -= 1.0;
            if (phase < 0.0)  phase += 1.0;

            if (++writePos >= size)
                writePos = 0;

            // A head that has just jumped is silent under its window, which makes this the
            // moment to choose where it lands. Head one wraps with the phase; head two when
            // the phase crosses a half.
            if (align && step != 0.0)
            {
                if (wrapped)
                    offset[0] = bestOffset(phase, offset[1], size, 0);
                else if ((before - 0.5) * (phase - 0.5) < 0.0)
                    offset[1] = bestOffset(phase < 0.5 ? phase + 0.5 : phase - 0.5, offset[0], size, 1);
            }
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

    /// Where the head that has just jumped should land so that it joins the other in phase.
    /// The last stretch the other head played is compared with the stretch behind each
    /// candidate landing, and the candidate that matches best wins. Searching a quarter
    /// of a grain either side keeps the head's delay near what its phase says.
    int bestOffset(double newPhase, int otherOffset, int size, int self) const noexcept
    {
        const int window = grain / 4;                 // how much to compare
        const int reach  = grain / 5;                 // how far to search
        const double otherPhase = newPhase < 0.5 ? newPhase + 0.5 : newPhase - 0.5;
        const int base  = static_cast<int>(newPhase * grain);
        const int other = static_cast<int>(otherPhase * grain) + otherOffset;
        (void) self;

        const auto at = [&](int delay)
        {
            int index = writePos - 1 - std::clamp(delay, 0, size - 2);
            if (index < 0) index += size;
            return line[static_cast<std::size_t>(index)];
        };

        double bestScore = -1.0e30;
        int best = 0;
        for (int candidate = -reach; candidate <= reach; candidate += 2)
        {
            const int start = base + candidate;
            if (start < 0 || start + window >= size - 2)
                continue;

            double cross = 0.0, energy = 1.0e-9;
            for (int k = 0; k < window; k += 2)
            {
                const double a = at(start + k);
                cross  += a * at(other + k);
                energy += a * a;
            }
            const double score = cross / std::sqrt(energy);
            if (score > bestScore)
            {
                bestScore = score;
                best = candidate;
            }
        }
        return best;
    }

    /// The line `p` of a grain ago, plus `extra` samples, linearly interpolated.
    float read(double p, int size, int extra) const noexcept
    {
        const double delay = std::clamp(p * grain + extra, 0.0, static_cast<double>(size - 3));
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
    int offset[2] = {0, 0};   ///< where each head's last jump was placed, in samples
    int pitchIndex = -1, mixIndex = -1, alignIndex = -1;
};

/// Frequency shifter: every partial moves by the same number of hertz, so a harmonic
/// series stops being one. The input is split into two copies a quarter turn apart by a
/// pair of allpass chains (Olli Niemitalo's twelve-pole IIR Hilbert transformer, accurate
/// from about 0.002 to 0.998 of Nyquist), and those are multiplied by a cosine and a
/// sine of the shift frequency and combined, which is single-sideband modulation.
/// The allpass chains delay by an amount that varies with frequency, so the dry signal is
/// not aligned with the shifted one and no latency is declared.
class FreqShifter final : public DspElement
{
    static constexpr int kSections = 4;

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        shiftIndex    = controlIndex(type, "shift");
        feedbackIndex = controlIndex(type, "feedback");
        mixIndex      = controlIndex(type, "mix");
        reset();
    }

    void reset() override
    {
        for (auto& chain : state)
            for (auto& s : chain)
                s = Section{};
        quadratureDelay = 0.0;
        angle = 0.0;
        previous = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const double nyquist = 0.5 * sampleRate;
        const double shift   = std::clamp(static_cast<double>(controlAt(args, shiftIndex, 0.0f)),
                                          -nyquist, nyquist);
        const float feedback = std::clamp(controlAt(args, feedbackIndex, 0.0f), 0.0f, 0.95f);
        const float mix      = std::clamp(controlAt(args, mixIndex, 1.0f), 0.0f, 1.0f);
        const double step    = 2.0 * 3.14159265358979323846 * shift / sampleRate;

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            const double x = static_cast<double>(in[i]) + static_cast<double>(feedback * previous);

            double inPhase = x;
            double quad    = x;
            for (int k = 0; k < kSections; ++k)
            {
                inPhase = state[0][static_cast<std::size_t>(k)].run(inPhase, kCoefficientsI[k]);
                quad    = state[1][static_cast<std::size_t>(k)].run(quad,    kCoefficientsQ[k]);
            }
            // The second chain is one sample late; the extra delay makes the pair a
            // quarter turn apart.
            const double delayedI = quadratureDelay;
            quadratureDelay = inPhase;

            const double shifted = delayedI * std::cos(angle) + quad * std::sin(angle);
            angle += step;
            if (angle > 3.14159265358979323846)       angle -= 2.0 * 3.14159265358979323846;
            else if (angle < -3.14159265358979323846) angle += 2.0 * 3.14159265358979323846;

            previous = static_cast<float>(shifted);
            out[i] = in[i] * (1.0f - mix) + previous * mix;
        }
    }

private:
    /// One second-order allpass: y = a2 * (x + y[-2]) - x[-2].
    struct Section
    {
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

        double run(double x, double a2) noexcept
        {
            const double y = a2 * (x + y2) - x2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    // Niemitalo's coefficients, already squared.
    static constexpr double kCoefficientsI[kSections] = {
        0.6923878 * 0.6923878, 0.9360654322959 * 0.9360654322959,
        0.9882295226860 * 0.9882295226860, 0.9987488452737 * 0.9987488452737 };
    static constexpr double kCoefficientsQ[kSections] = {
        0.4021921162426 * 0.4021921162426, 0.8561710882420 * 0.8561710882420,
        0.9722909545651 * 0.9722909545651, 0.9952884791278 * 0.9952884791278 };

    double sampleRate = 44100.0;
    Section state[2][kSections];
    double quadratureDelay = 0.0;
    double angle = 0.0;
    float previous = 0.0f;
    int shiftIndex = -1, feedbackIndex = -1, mixIndex = -1;
};

/// Tape: the four things a recording on tape does to a sound that a plain saturator
/// does not. In order along the path:
///
///   saturation   tanh, normalised so a quiet signal passes at unity gain; at zero
///                drive it is nearly linear for signals up to about 0.5
///   head bump    a broad low boost near 80 Hz, from the playback head's response
///   wow/flutter  a short delay whose length moves, which is speed variation: a slow
///                sway (wow, under 2 Hz), a fast one (flutter, near 9 Hz) and a little
///                random drift
///   loss + hiss  a one-pole high cut for the tape's limited bandwidth, and the
///                noise it carries, which passes through the same cut
///
/// The path adds a fixed 3 ms, which is the middle of the delay the modulation moves
/// around, and that is declared as latency.
class Tape final : public DspElement
{
    static constexpr double kBaseMs = 3.0;
    static constexpr double kPi = 3.14159265358979323846;

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        driveIndex   = controlIndex(type, "drive");
        bumpIndex    = controlIndex(type, "bump");
        wowIndex     = controlIndex(type, "wow");
        flutterIndex = controlIndex(type, "flutter");
        hfIndex      = controlIndex(type, "hf");
        hissIndex    = controlIndex(type, "hiss");
        seedIndex    = controlIndex(type, "seed");

        base = kBaseMs * 0.001 * rate;
        line.assign(static_cast<std::size_t>(base * 2.0) + 16, 0.0f);
        reset();
    }

    void reset() override
    {
        std::fill(line.begin(), line.end(), 0.0f);
        writePos = 0;
        wowPhase = flutterPhase = 0.0;
        drift = 0.0;
        loss = 0.0;
        z1 = z2 = 0.0;
        reseeded = false;
    }

    int latencyInSamples() const override { return static_cast<int>(std::lround(base)); }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const auto seed = static_cast<std::uint32_t>(std::max(controlAt(args, seedIndex, 1.0f), 0.0f));
        if (! reseeded || seed != lastSeed)
        {
            random.seed(seed * 2654435761u + 0x9E3779B9u);
            lastSeed = seed;
            reseeded = true;
        }

        const float  driveDb = std::clamp(controlAt(args, driveIndex, 6.0f), 0.0f, 30.0f);
        const float  bumpDb  = std::clamp(controlAt(args, bumpIndex, 3.0f), 0.0f, 12.0f);
        const double wow     = std::clamp(static_cast<double>(controlAt(args, wowIndex, 0.3f)), 0.0, 1.0);
        const double flutter = std::clamp(static_cast<double>(controlAt(args, flutterIndex, 0.2f)), 0.0, 1.0);
        const double hf      = std::clamp(static_cast<double>(controlAt(args, hfIndex, 14000.0f)), 1000.0, 20000.0);
        const float  hissDb  = std::clamp(controlAt(args, hissIndex, -80.0f), -120.0f, -20.0f);

        // 0.3 puts a signal of 0.5 well inside the linear part of the curve at zero drive.
        const double gain     = 0.3 * std::pow(10.0, static_cast<double>(driveDb) / 20.0);
        const double hissGain = std::pow(10.0, static_cast<double>(hissDb) / 20.0);
        setBump(bumpDb);

        // Speed variation as a depth in samples: a fractional speed error e at
        // frequency f moves the delay by e / (2 pi f).
        const double wowDepth     = wow     * 0.005 / (2.0 * kPi * 0.8) * sampleRate;
        const double flutterDepth = flutter * 0.003 / (2.0 * kPi * 9.0) * sampleRate;
        const double driftDepth   = (wow + flutter) * 2.0;

        const double lossCoefficient = 1.0 - std::exp(-2.0 * kPi * hf / sampleRate);
        const int    size = static_cast<int>(line.size());

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            // Saturation, unity gain for small signals.
            const double x = std::tanh(gain * static_cast<double>(in[i])) / gain;

            // Head bump (transposed direct form II).
            const double bumped = b0 * x + z1;
            z1 = b1 * x - a1 * bumped + z2;
            z2 = b2 * x - a2 * bumped;

            line[static_cast<std::size_t>(writePos)] = static_cast<float>(bumped);

            drift += 0.0005 * (random.bipolar() * 40.0 - drift);
            wowPhase     += 0.8 / sampleRate;
            flutterPhase += 9.0 / sampleRate;
            wowPhase     -= std::floor(wowPhase);
            flutterPhase -= std::floor(flutterPhase);

            const double modulation = wowDepth * std::sin(2.0 * kPi * wowPhase)
                                    + flutterDepth * std::sin(2.0 * kPi * flutterPhase)
                                    + driftDepth * drift;
            const double delay = std::clamp(base + modulation, 1.0, base * 2.0 - 2.0);

            const int   whole = static_cast<int>(delay);
            const float frac  = static_cast<float>(delay - whole);
            int a = writePos - whole;
            if (a < 0) a += size;
            int b = a - 1;
            if (b < 0) b += size;
            const double played = static_cast<double>(line[static_cast<std::size_t>(a)]) * (1.0 - frac)
                                + static_cast<double>(line[static_cast<std::size_t>(b)]) * frac;

            loss += lossCoefficient * (played + hissGain * static_cast<double>(random.bipolar()) - loss);
            out[i] = static_cast<float>(loss);

            if (++writePos >= size)
                writePos = 0;
        }
    }

private:
    /// RBJ peaking filter at 80 Hz, Q of 0.7, so the boost is broad.
    /// https://www.w3.org/TR/audio-eq-cookbook/
    void setBump(float gainDb) noexcept
    {
        if (gainDb == bumpDb)
            return;
        bumpDb = gainDb;

        const double A     = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
        const double w0    = 2.0 * kPi * 80.0 / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * 0.7);
        const double a0    = 1.0 + alpha / A;

        b0 = (1.0 + alpha * A) / a0;
        b1 = (-2.0 * std::cos(w0)) / a0;
        b2 = (1.0 - alpha * A) / a0;
        a1 = b1;
        a2 = (1.0 - alpha / A) / a0;
    }

    double sampleRate = 48000.0, base = 144.0;
    std::vector<float> line;
    int writePos = 0;
    double wowPhase = 0.0, flutterPhase = 0.0, drift = 0.0, loss = 0.0;
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
    float bumpDb = 0.0f;
    dsp::Random random;
    std::uint32_t lastSeed = 0;
    bool reseeded = false;
    int driveIndex = -1, bumpIndex = -1, wowIndex = -1, flutterIndex = -1,
        hfIndex = -1, hissIndex = -1, seedIndex = -1;
};

/// Bucket-brigade delay, after the Matsushita MN3005 in the Boss DD-3.
///
/// A bucket-brigade device passes each sample along a chain of 4096 stages, one step per
/// clock edge, so its delay is the number of stages over twice the clock rate. Three
/// things follow from that, and they are the whole character of the sound:
///
///   Bandwidth moves with delay. The clock is also the sampling rate, so the filters
///   around the chain that keep the clock out must sit below half of it. A long delay
///   needs a slow clock, so it is dark; a short one is bright. Here both filters follow
///   the delay. `bandwidth` is the corner as a fraction of the clock rate.
///
///   Repeats darken. Feedback takes the output back through the same filters, so every
///   trip round the loop removes more top end, which is why a BBD echo sits back in a mix.
///
///   Companding. The chain is noisy, so the signal is compressed 2:1 in decibels on the
///   way in and expanded on the way out, which turns the noise down whenever the signal is
///   quiet and gives the pedal its slight pump. The pair is exact for a signal passed
///   through, and is applied around the chain only.
///
/// Changing the time moves the clock, which bends the pitch of what is in the chain, as on
/// the pedal. What is not modelled: the range switch and hold mode, the clock's own
/// leakage into the audio, and the stage-to-stage losses of a real chain.
class BucketBrigade final : public DspElement
{
    static constexpr double kStages = 4096.0;
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kMaxMs = 800.0;

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        timeIndex     = controlIndex(type, "time");
        feedbackIndex = controlIndex(type, "feedback");
        bandwidthIndex = controlIndex(type, "bandwidth");
        noiseIndex    = controlIndex(type, "noise");
        companderIndex = controlIndex(type, "compander");
        seedIndex     = controlIndex(type, "seed");

        line.assign(static_cast<std::size_t>(kMaxMs * 0.001 * rate) + 8, 0.0f);
        reset();
    }

    void reset() override
    {
        std::fill(line.begin(), line.end(), 0.0f);
        writePos = 0;
        delay = -1.0;
        for (auto& f : pre) f = Section{};
        post = Section{};
        lastCorner = 0.0;   // the sections are back to identity, so the next block sets them again
        compressEnv = expandEnv = 0.0;
        reseeded = false;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const auto seed = static_cast<std::uint32_t>(std::max(controlAt(args, seedIndex, 1.0f), 0.0f));
        if (! reseeded || seed != lastSeed)
        {
            random.seed(seed * 2654435761u + 0x9E3779B9u);
            lastSeed = seed;
            reseeded = true;
        }

        const double timeMs   = std::clamp(static_cast<double>(controlAt(args, timeIndex, 300.0f)), 5.0, kMaxMs);
        const double feedback = std::clamp(static_cast<double>(controlAt(args, feedbackIndex, 0.3f)), 0.0, 1.1);
        const double ratio    = std::clamp(static_cast<double>(controlAt(args, bandwidthIndex, 0.35f)), 0.05, 1.0);
        const double noise    = std::pow(10.0, std::clamp(static_cast<double>(controlAt(args, noiseIndex, -75.0f)), -120.0, -30.0) / 20.0);
        const bool   compand  = controlAt(args, companderIndex, 1.0f) > 0.5f;

        const double target = timeMs * 0.001 * sampleRate;
        if (delay < 0.0)
            delay = target;

        // Clock rate from the delay, and the corner of the filters from the clock.
        const double clock  = kStages / (2.0 * timeMs * 0.001);
        const double corner = std::clamp(ratio * clock, 200.0, 0.45 * sampleRate);
        setFilters(corner);

        const double slew = 1.0 / (0.03 * sampleRate);   // the clock settles in about 30 ms
        const double detector = 1.0 / (0.01 * sampleRate);
        const int    size = static_cast<int>(line.size());

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            delay += (target - delay) * slew;

            // The oldest end of the chain, read between two stages.
            const double d = std::clamp(delay, 2.0, static_cast<double>(size - 3));
            const int whole = static_cast<int>(d);
            const double frac = d - whole;
            int a = writePos - whole;
            if (a < 0) a += size;
            int b = a - 1;
            if (b < 0) b += size;
            const double tapped = static_cast<double>(line[static_cast<std::size_t>(a)]) * (1.0 - frac)
                                + static_cast<double>(line[static_cast<std::size_t>(b)]) * frac;

            // Out of the chain: noise joins the signal here, in the compressed domain.
            double y = tapped + noise * static_cast<double>(random.bipolar());
            y = post.run(y);

            double decoded = y;
            if (compand)
            {
                expandEnv += detector * (y * y - expandEnv);
                decoded = y * std::sqrt(expandEnv + 1.0e-8);
            }
            else
            {
                expandEnv = 0.0;
            }

            // Back into the chain: the input plus the repeat, kept bounded by a soft clip.
            const double fed = static_cast<double>(in[i]) + std::tanh(feedback * decoded);
            double x = fed;
            if (compand)
            {
                compressEnv += detector * (fed * fed - compressEnv);
                x = fed / std::sqrt(std::sqrt(compressEnv + 1.0e-8));
            }
            else
            {
                compressEnv = 0.0;
            }

            for (auto& f : pre)
                x = f.run(x);
            line[static_cast<std::size_t>(writePos)] = static_cast<float>(x);

            out[i] = static_cast<float>(decoded);
            if (++writePos >= size)
                writePos = 0;
        }
    }

private:
    /// One biquad low-pass section, transposed direct form II.
    struct Section
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

        double run(double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void lowpass(double hz, double q, double rate) noexcept
        {
            const double w0 = 2.0 * kPi * hz / rate;
            const double alpha = std::sin(w0) / (2.0 * q);
            const double c = std::cos(w0);
            const double a0 = 1.0 + alpha;
            b0 = (1.0 - c) * 0.5 / a0;
            b1 = (1.0 - c) / a0;
            b2 = b0;
            a1 = -2.0 * c / a0;
            a2 = (1.0 - alpha) / a0;
        }
    };

    /// Fourth-order Butterworth before the chain, second order after it.
    void setFilters(double corner) noexcept
    {
        if (corner == lastCorner)
            return;
        lastCorner = corner;
        pre[0].lowpass(corner, 0.5412, sampleRate);
        pre[1].lowpass(corner, 1.3066, sampleRate);
        post.lowpass(corner, 0.7071, sampleRate);
    }

    double sampleRate = 48000.0, delay = -1.0, lastCorner = 0.0;
    double compressEnv = 0.0, expandEnv = 0.0;
    std::vector<float> line;
    int writePos = 0;
    Section pre[2], post;
    dsp::Random random;
    std::uint32_t lastSeed = 0;
    bool reseeded = false;
    int timeIndex = -1, feedbackIndex = -1, bandwidthIndex = -1, noiseIndex = -1,
        companderIndex = -1, seedIndex = -1;
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
    registry.add("FreeModalBank", &make<elements::FreeModalBank>);
    registry.add("Chorus",        &make<elements::Chorus>);
    registry.add("Flanger",       &make<elements::Flanger>);
    registry.add("Phaser",        &make<elements::Phaser>);
    registry.add("Biquad",        &make<elements::Biquad>);
    registry.add("Crossover",     &make<elements::Crossover>);
    registry.add("Redux",         &make<elements::Redux>);
    registry.add("PitchShift",    &make<elements::PitchShift>);
    registry.add("FreqShifter",   &make<elements::FreqShifter>);
    registry.add("Tape",          &make<elements::Tape>);
    registry.add("BucketBrigade", &make<elements::BucketBrigade>);
}
}  // namespace valis

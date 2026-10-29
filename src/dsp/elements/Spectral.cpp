// src/dsp/elements/Spectral.cpp
//
// Elements that work on a window of samples rather than on one sample at a
// time. The survey calls this a second execution domain, and it is: an FFT has
// a window, a hop and a latency, none of which a scalar recurrence has.
//
// Valis needs no new engine machinery for it. An element is already an opaque
// boundary with a real-time contract, so a block algorithm lives inside one:
// it buffers what arrives, transforms every hop, overlap-adds the result, and
// declares the latency that costs. The engine sums declared latency per circuit
// and reports it to the host, so the delay is compensated rather than hidden.
//
// The pattern to copy for a new spectral element is SpectralGate below: own the
// FIFOs, allocate them in prepare(), and do arithmetic only in processFrame().

#include "Common.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <vector>

namespace valis::elements {

/// Short-time Fourier transform with overlap-add resynthesis.
///
/// The window is Hann, applied on analysis and again on synthesis, with a hop of
/// a quarter of the window. That pair sums to a constant across frames, so a
/// frame left untouched resynthesises the input unchanged; `normalisation`
/// measures that constant rather than assuming it.
///
/// See https://en.wikipedia.org/wiki/Short-time_Fourier_transform for the
/// transform, and Zölzer, "DAFX" (Wiley, 2011), chapter 7 for the overlap-add
/// arrangement this follows.
class StftEngine
{
public:
    /// Message thread. Everything this element will ever need is allocated here.
    void prepare(int fftOrder)
    {
        order      = juce::jlimit(6, 13, fftOrder);
        windowSize = 1 << order;
        hop        = windowSize / 4;

        fft = std::make_unique<juce::dsp::FFT>(order);

        window.resize(static_cast<std::size_t>(windowSize));
        for (int i = 0; i < windowSize; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * i / windowSize;
            window[static_cast<std::size_t>(i)] =
                static_cast<float>(0.5 - 0.5 * std::cos(phase));
        }

        // The window squared, overlap-added at the hop, is what a frame that is
        // left alone comes out multiplied by. Measure it rather than quoting a
        // constant that only holds for one window and hop.
        double sum = 0.0;
        for (int offset = 0; offset < windowSize; offset += hop)
        {
            const auto w = window[static_cast<std::size_t>(offset)];
            sum += static_cast<double>(w) * w;
        }
        normalisation = sum > 0.0 ? static_cast<float>(1.0 / sum) : 1.0f;

        // Real-only transforms read and write 2N floats.
        frame.assign(static_cast<std::size_t>(windowSize) * 2, 0.0f);
        input.assign(static_cast<std::size_t>(windowSize), 0.0f);
        output.assign(static_cast<std::size_t>(windowSize), 0.0f);

        reset();
    }

    void reset() noexcept
    {
        std::fill(input.begin(), input.end(), 0.0f);
        std::fill(output.begin(), output.end(), 0.0f);
        std::fill(frame.begin(), frame.end(), 0.0f);
        position  = 0;
        untilNext = 0;
    }

    int latency() const noexcept { return windowSize; }
    int size() const noexcept { return windowSize; }

    /// Audio thread. `modify` is handed the interleaved real/imaginary bins and
    /// must not allocate. One sample in, one sample out, delayed by latency().
    template <typename Modify>
    void process(const float* in, float* out, int numSamples, Modify&& modify) noexcept
    {
        if (windowSize == 0)
            return;

        const auto n = static_cast<std::size_t>(windowSize);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto at = static_cast<std::size_t>(position);

            input[at] = in[i];
            out[i]    = output[at];
            output[at] = 0.0f;

            position = (position + 1) % windowSize;

            if (++untilNext >= hop)
            {
                untilNext = 0;
                processFrame(std::forward<Modify>(modify));
            }
        }

        juce::ignoreUnused(n);
    }

private:
    /// Takes the whole window ending at the write position, transforms it,
    /// hands the bins to `modify`, and overlap-adds the result back.
    template <typename Modify>
    void processFrame(Modify&& modify) noexcept
    {
        // The oldest sample in the circular buffer is the one about to be
        // overwritten, so the frame starts there.
        for (int i = 0; i < windowSize; ++i)
        {
            const auto from = static_cast<std::size_t>((position + i) % windowSize);
            frame[static_cast<std::size_t>(i)] =
                input[from] * window[static_cast<std::size_t>(i)];
        }
        std::fill(frame.begin() + windowSize, frame.end(), 0.0f);

        fft->performRealOnlyForwardTransform(frame.data());
        modify(frame.data(), windowSize / 2 + 1);
        fft->performRealOnlyInverseTransform(frame.data());

        for (int i = 0; i < windowSize; ++i)
        {
            const auto to = static_cast<std::size_t>((position + i) % windowSize);
            output[to] += frame[static_cast<std::size_t>(i)]
                        * window[static_cast<std::size_t>(i)] * normalisation;
        }
    }

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, frame, input, output;

    int   order         = 10;
    int   windowSize    = 0;
    int   hop           = 0;
    int   position      = 0;
    int   untilNext     = 0;
    float normalisation = 1.0f;
};

/// Spectral noise gate: a bin quieter than the threshold is removed, the rest
/// pass through. This is not a thing a time-domain filter can do, which is the
/// point of having the domain at all - it removes quiet broadband noise from
/// underneath a loud tone instead of removing a band of frequencies.
class SpectralGate final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate     = rate;
        thresholdIndex = controlIndex(type, "threshold");
        allocate();
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error) override
    {
        if (key != "fftSize")
            return true;

        const int size = juce::String(std::string(value)).getIntValue();
        if (size < 64 || size > 8192 || (size & (size - 1)) != 0)
        {
            error = "val:fftSize must be a power of two between 64 and 8192, not '" +
                    std::string(value) + "'";
            return false;
        }

        order = static_cast<int>(std::log2(static_cast<double>(size)));
        allocate();
        return true;
    }

    void reset() override
    {
        stft.reset();
        std::fill(gainState.begin(), gainState.end(), 1.0f);
        std::fill(power.begin(), power.end(), 0.0f);
    }

    int latencyInSamples() const override { return stft.latency(); }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        // The threshold is a level in dB relative to full scale. A bin's
        // magnitude is compared against it, so the control reads the same way
        // whatever the window size, which a raw bin value would not.
        const float thresholdDb = controlAt(args, thresholdIndex, -60.0f);
        const float floorMag = juce::Decibels::decibelsToGain(thresholdDb, -120.0f)
                             * static_cast<float>(stft.size()) * 0.5f;

        const float floorPower = floorMag * floorMag;

        float* smoothed = power.data();
        float* held     = gainState.data();

        stft.process(args.audioIn[0], args.audioOut[0], args.numSamples,
                     [floorPower, smoothed, held](float* bins, int numBins) noexcept
                     {
                         for (int b = 0; b < numBins; ++b)
                         {
                             const float re = bins[b * 2];
                             const float im = bins[b * 2 + 1];
                             smoothed[b] = re * re + im * im;
                         }

                         // Smoothed across neighbouring bins before the gain is
                         // decided. A tone that does not sit exactly on a bin
                         // spreads into the ones beside it, and gating those
                         // skirts away truncates the tone: the truncation rings
                         // back as broadband noise, so the gate would add more
                         // than it removed.
                         float previous = smoothed[0];
                         for (int b = 0; b < numBins; ++b)
                         {
                             const float next    = b + 1 < numBins ? smoothed[b + 1] : smoothed[b];
                             const float current = smoothed[b];
                             const float wide    = 0.25f * previous + 0.5f * current + 0.25f * next;
                             previous = current;

                             // The Wiener gain: near one well above the
                             // threshold, falling away as the ratio below it.
                             // https://en.wikipedia.org/wiki/Wiener_filter
                             // A silent bin under a threshold of silence would
                             // divide zero by zero, so it is left alone.
                             const float target = wide > 0.0f ? wide / (wide + floorPower) : 0.0f;

                             // And smoothed again over time. Without this the
                             // gain of a bin moves frame to frame, which
                             // amplitude-modulates whatever is in it at the hop
                             // rate and puts sidebands either side of it.
                             held[b] += 0.5f * (target - held[b]);

                             bins[b * 2]     *= held[b];
                             bins[b * 2 + 1] *= held[b];
                         }
                     });
    }

private:
    /// Message thread. The per-bin state the gain smoothing needs, sized once
    /// so process() only reads and writes it.
    void allocate()
    {
        stft.prepare(order);

        const auto bins = static_cast<std::size_t>(stft.size() / 2 + 1);
        gainState.assign(bins, 1.0f);
        power.assign(bins, 0.0f);
    }

    StftEngine stft;

    /// Per bin: the gain carried between frames, and scratch for the magnitudes
    /// the gain is decided from.
    std::vector<float> gainState, power;

    double sampleRate    = 48000.0;
    int    order         = 10;   ///< 1024 by default
    int    thresholdIndex = -1;
};

/// Spectral delay: each frequency bin is delayed by its own time, so the spectrum
/// arrives spread out like a chirp rather than all at once. Delay is in whole STFT
/// frames, which is exact for every bin (a frame shifted by a hop is the frame of
/// the signal shifted by a hop), so the resolution is one hop: 5 ms at 1024 points
/// and 48 kHz.
///
/// The delay of bin b is time * (1 + spread * (2x - 1)), with x the bin's place
/// from 0 at DC to 1 at Nyquist: a positive spread delays the highs more than the
/// lows, a negative one the reverse, and zero is a plain delay.
class SpectralDelay final : public DspElement
{
    static constexpr float kMaxFeedback = 0.95f;
    static constexpr double kMaxSeconds = 2.0;   // time at full spread is twice val:time

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        timeIndex     = controlIndex(type, "time");
        spreadIndex   = controlIndex(type, "spread");
        feedbackIndex = controlIndex(type, "feedback");
        mixIndex      = controlIndex(type, "mix");
        allocate();
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error) override
    {
        if (key != "fftSize")
            return true;

        const int size = juce::String(std::string(value)).getIntValue();
        if (size < 64 || size > 8192 || (size & (size - 1)) != 0)
        {
            error = "val:fftSize must be a power of two between 64 and 8192, not '" +
                    std::string(value) + "'";
            return false;
        }

        order = static_cast<int>(std::log2(static_cast<double>(size)));
        allocate();
        return true;
    }

    void reset() override
    {
        stft.reset();
        std::fill(history.begin(), history.end(), 0.0f);
        slot = 0;
    }

    int latencyInSamples() const override { return stft.latency(); }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1 || history.empty())
            return;

        const double timeMs   = std::clamp(static_cast<double>(controlAt(args, timeIndex, 200.0f)), 0.0, 1000.0);
        const float  spread   = std::clamp(controlAt(args, spreadIndex, 0.5f), -1.0f, 1.0f);
        const float  feedback = std::clamp(controlAt(args, feedbackIndex, 0.3f), 0.0f, kMaxFeedback);
        const float  mix      = std::clamp(controlAt(args, mixIndex, 0.5f), 0.0f, 1.0f);

        const int numBins = stft.size() / 2 + 1;
        const double framesPerMs = sampleRate * 0.001 / static_cast<double>(stft.size() / 4);
        for (int b = 0; b < numBins; ++b)
        {
            const double x = static_cast<double>(b) / static_cast<double>(numBins - 1);
            const double ms = timeMs * (1.0 + static_cast<double>(spread) * (2.0 * x - 1.0));
            delays[static_cast<std::size_t>(b)] =
                std::clamp(static_cast<int>(std::lround(ms * framesPerMs)), 0, frames - 1);
        }

        stft.process(args.audioIn[0], args.audioOut[0], args.numSamples,
                     [this, feedback, mix](float* bins, int count) noexcept
                     {
                         const auto stride = static_cast<std::size_t>(count) * 2;
                         float* mine = history.data() + static_cast<std::size_t>(slot) * stride;

                         for (int b = 0; b < count; ++b)
                         {
                             const int d = delays[static_cast<std::size_t>(b)];
                             const float re = bins[b * 2];
                             const float im = bins[b * 2 + 1];

                             float wetRe = re, wetIm = im;
                             if (d > 0)
                             {
                                 const int from = (slot - d + frames) % frames;
                                 const float* there = history.data() + static_cast<std::size_t>(from) * stride;
                                 wetRe = there[b * 2];
                                 wetIm = there[b * 2 + 1];
                             }

                             // What is stored is the input plus the repeat of what
                             // arrives from the past, so each bin is its own delay line.
                             mine[b * 2]     = re + (d > 0 ? feedback * wetRe : 0.0f);
                             mine[b * 2 + 1] = im + (d > 0 ? feedback * wetIm : 0.0f);

                             bins[b * 2]     = re + mix * (wetRe - re);
                             bins[b * 2 + 1] = im + mix * (wetIm - im);
                         }

                         slot = (slot + 1) % frames;
                     });
    }

private:
    void allocate()
    {
        stft.prepare(order);

        const int numBins = stft.size() / 2 + 1;
        frames = std::max(2, static_cast<int>(std::ceil(kMaxSeconds * sampleRate / (stft.size() / 4))) + 2);
        history.assign(static_cast<std::size_t>(frames) * static_cast<std::size_t>(numBins) * 2, 0.0f);
        delays.assign(static_cast<std::size_t>(numBins), 0);
        slot = 0;
    }

    StftEngine stft;
    std::vector<float> history;   ///< frames x bins x (re, im): what each bin has said so far
    std::vector<int>   delays;
    double sampleRate = 48000.0;
    int order = 10, frames = 0, slot = 0;
    int timeIndex = -1, spreadIndex = -1, feedbackIndex = -1, mixIndex = -1;
};

/// Low-bitrate coding artefacts, approximated in the spectral domain: bins above a
/// bandwidth are removed, bins far below the frame's loudest are dropped, and the
/// magnitudes that remain are rounded to steps in decibels. Because the set of bins
/// that survives changes from frame to frame, what is left warbles, which is the
/// sound of a coarse codec. It is not a model of any one encoder: no psychoacoustic
/// masking, no bit allocation, no transform of the codec's own.
class Codec final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate     = rate;
        bandwidthIndex = controlIndex(type, "bandwidth");
        stepIndex      = controlIndex(type, "step");
        floorIndex     = controlIndex(type, "floor");
        mixIndex       = controlIndex(type, "mix");
        stft.prepare(10);
    }

    void reset() override { stft.reset(); }

    int latencyInSamples() const override { return stft.latency(); }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const double nyquist   = sampleRate * 0.5;
        const double bandwidth = std::clamp(static_cast<double>(controlAt(args, bandwidthIndex, 12000.0f)), 500.0, nyquist);
        const float  step      = std::clamp(controlAt(args, stepIndex, 3.0f), 0.0f, 24.0f);
        const float  floorDb   = std::clamp(controlAt(args, floorIndex, 30.0f), 0.0f, 90.0f);
        const float  mix       = std::clamp(controlAt(args, mixIndex, 1.0f), 0.0f, 1.0f);

        const int numBins = stft.size() / 2 + 1;
        const int last    = std::min(numBins - 1, static_cast<int>(bandwidth / nyquist * (numBins - 1)));

        stft.process(args.audioIn[0], args.audioOut[0], args.numSamples,
                     [last, step, floorDb, mix](float* bins, int count) noexcept
                     {
                         float peak = 0.0f;
                         for (int b = 0; b < count; ++b)
                             peak = std::max(peak, bins[b * 2] * bins[b * 2] + bins[b * 2 + 1] * bins[b * 2 + 1]);

                         const float floorPower = floorDb > 0.0f
                             ? peak * std::pow(10.0f, -floorDb * 0.1f)
                             : 0.0f;

                         for (int b = 0; b < count; ++b)
                         {
                             const float re = bins[b * 2];
                             const float im = bins[b * 2 + 1];
                             const float power = re * re + im * im;

                             float gain = 1.0f;
                             if (b > last || power <= floorPower || power <= 0.0f)
                                 gain = 0.0f;
                             else if (step > 0.0f)
                             {
                                 const float db = 10.0f * std::log10(power);
                                 const float rounded = std::round(db / step) * step;
                                 gain = std::pow(10.0f, (rounded - db) * 0.05f);
                             }

                             gain = 1.0f + mix * (gain - 1.0f);
                             bins[b * 2]     = re * gain;
                             bins[b * 2 + 1] = im * gain;
                         }
                     });
    }

private:
    StftEngine stft;
    double sampleRate = 48000.0;
    int bandwidthIndex = -1, stepIndex = -1, floorIndex = -1, mixIndex = -1;
};

/// Linear predictive cross-synthesis. A modulator (a voice, say) and a carrier (a
/// buzz or noise) go in; the carrier comes out shaped by the modulator's spectral
/// envelope, so the buzz speaks.
///
/// Every hop the last window of each input is fitted with an all-pole model by the
/// autocorrelation method and Levinson-Durbin recursion, which always gives a stable
/// filter. The carrier is whitened by the inverse of its own model, so whatever
/// shape it had is removed, then driven through the modulator's model and scaled by
/// the ratio of the two prediction errors, which carries the modulator's loudness.
/// There is no latency. See https://en.wikipedia.org/wiki/Linear_predictive_coding
class Lpc final : public DspElement
{
    static constexpr int kWindow = 1024;   // a power of two, for the ring masks
    static constexpr int kHop    = 128;
    static constexpr int kMaxOrder = 32;
    static constexpr double kExpansion = 0.998;   // pulls poles in from the unit circle

public:
    void prepare(const ElementType& type, double, int) override
    {
        orderIndex = controlIndex(type, "order");
        mixIndex   = controlIndex(type, "mix");

        for (int i = 0; i < kWindow; ++i)
            window[static_cast<std::size_t>(i)] =
                static_cast<float>(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / kWindow));
        reset();
    }

    void reset() override
    {
        modRing.fill(0.0f); carRing.fill(0.0f); synthRing.fill(0.0f);
        modCoef.fill(0.0); carCoef.fill(0.0);
        position  = 0;
        untilNext = 0;
        gain = 0.0f;
        targetGain = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 2 || args.numAudioOut < 1)
        {
            if (args.numAudioIn >= 1 && args.numAudioOut >= 1)
                std::copy(args.audioIn[0], args.audioIn[0] + args.numSamples, args.audioOut[0]);
            return;
        }

        const int   order = std::clamp(static_cast<int>(controlAt(args, orderIndex, 16.0f) + 0.5f), 2, kMaxOrder);
        const float mix   = std::clamp(controlAt(args, mixIndex, 1.0f), 0.0f, 1.0f);

        const float* modulator = args.audioIn[0];
        const float* carrier   = args.audioIn[1];
        float*       out       = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            const auto at = static_cast<std::size_t>(position);
            modRing[at] = modulator[i];
            carRing[at] = carrier[i];

            // The carrier's residual: the part of it its own past does not predict.
            double residual = carrier[i];
            for (int k = 1; k <= order; ++k)
                residual += carCoef[static_cast<std::size_t>(k)]
                          * carRing[static_cast<std::size_t>((position - k) & (kWindow - 1))];

            // Through the modulator's model: y = g e - sum a_k y[n-k].
            double y = static_cast<double>(gain) * residual;
            for (int k = 1; k <= order; ++k)
                y -= modCoef[static_cast<std::size_t>(k)]
                   * synthRing[static_cast<std::size_t>((position - k) & (kWindow - 1))];

            synthRing[at] = static_cast<float>(y);
            out[i] = carrier[i] + mix * (static_cast<float>(y) - carrier[i]);

            gain += 0.02f * (targetGain - gain);
            position = (position + 1) & (kWindow - 1);

            if (++untilNext >= kHop)
            {
                untilNext = 0;
                analyse(order);
            }
        }
    }

private:
    /// Fits both models to the window ending at the write position.
    void analyse(int order) noexcept
    {
        double modError = 0.0, carError = 0.0;
        const bool modOk = fit(modRing, order, modCoef, modError);
        const bool carOk = fit(carRing, order, carCoef, carError);

        targetGain = (modOk && carOk) ? static_cast<float>(std::sqrt(modError / carError)) : 0.0f;
    }

    bool fit(const std::array<float, kWindow>& ring, int order,
             std::array<double, kMaxOrder + 1>& coef, double& error) noexcept
    {
        for (int i = 0; i < kWindow; ++i)
            frame[static_cast<std::size_t>(i)] =
                static_cast<double>(ring[static_cast<std::size_t>((position + i) & (kWindow - 1))])
                * static_cast<double>(window[static_cast<std::size_t>(i)]);

        std::array<double, kMaxOrder + 1> r{};
        for (int lag = 0; lag <= order; ++lag)
        {
            double sum = 0.0;
            for (int i = lag; i < kWindow; ++i)
                sum += frame[static_cast<std::size_t>(i)] * frame[static_cast<std::size_t>(i - lag)];
            r[static_cast<std::size_t>(lag)] = sum;
        }

        coef.fill(0.0);
        if (r[0] < 1.0e-9)
            return false;

        // A little white noise on the diagonal keeps the recursion well conditioned.
        r[0] *= 1.0001;

        std::array<double, kMaxOrder + 1> a{}, previous{};
        a[0] = 1.0;
        double err = r[0];
        for (int i = 1; i <= order && err > 0.0; ++i)
        {
            double acc = r[static_cast<std::size_t>(i)];
            for (int j = 1; j < i; ++j)
                acc += a[static_cast<std::size_t>(j)] * r[static_cast<std::size_t>(i - j)];

            const double k = -acc / err;
            previous = a;
            for (int j = 1; j < i; ++j)
                a[static_cast<std::size_t>(j)] = previous[static_cast<std::size_t>(j)]
                                               + k * previous[static_cast<std::size_t>(i - j)];
            a[static_cast<std::size_t>(i)] = k;
            err *= 1.0 - k * k;
        }

        double scale = 1.0;
        for (int k = 1; k <= order; ++k)
        {
            scale *= kExpansion;
            coef[static_cast<std::size_t>(k)] = a[static_cast<std::size_t>(k)] * scale;
        }

        error = std::max(err, 1.0e-12);
        return true;
    }

    std::array<float, kWindow> modRing{}, carRing{}, synthRing{}, window{};
    std::array<double, kWindow> frame{};
    std::array<double, kMaxOrder + 1> modCoef{}, carCoef{};
    int position = 0, untilNext = 0;
    float gain = 0.0f, targetGain = 0.0f;
    int orderIndex = -1, mixIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerSpectralElements(ElementRegistry& registry)
{
    registry.add("SpectralGate",  &make<elements::SpectralGate>);
    registry.add("SpectralDelay", &make<elements::SpectralDelay>);
    registry.add("Codec",         &make<elements::Codec>);
    registry.add("LPC",           &make<elements::Lpc>);
}

}  // namespace valis

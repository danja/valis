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

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerSpectralElements(ElementRegistry& registry)
{
    registry.add("SpectralGate", &make<elements::SpectralGate>);
}

}  // namespace valis

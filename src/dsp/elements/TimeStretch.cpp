// src/dsp/elements/TimeStretch.cpp
//
// val:TimeStretch plays a sound file at a different speed without changing its pitch,
// and at a different pitch without changing its speed, with the option of keeping
// the formants where they were. It is a phase vocoder: the file is cut into
// overlapping windowed frames, each is taken to the frequency domain, the phase of
// every partial is advanced by the amount that keeps it continuous at the new hop, and
// the frames are laid back down with overlap-add.
//
// A stretch changes the length of a signal, so this cannot be a filter with one sample
// out for one sample in. It reads a file, as val:Granulator and val:SampleLoad do, and the
// file is read on the message thread; everything the audio thread needs is allocated in
// prepare().
//
// Pitch is the stretch followed by a resampling: stretching by r keeps the pitch, and
// reading the result r times as fast restores the length and moves the pitch by r.
// Resampling moves the formants with the pitch, so when they are to stay put each frame is
// first multiplied by the ratio of its spectral envelope at the bin it will land on to its
// envelope where it is. The envelope is the cepstrally smoothed spectrum.
//
// Phase locking follows Laroche and Dolson, "Improved phase vocoder time-scale
// modification of audio" (IEEE Trans. Speech and Audio Processing, 1999): partials are
// found as peaks of the magnitude, a peak's phase is advanced by its measured frequency,
// and the bins around it keep their phase relation to it. That keeps a vowel from
// turning into the "phasiness" of a plain phase vocoder.
// https://en.wikipedia.org/wiki/Phase_vocoder

#include "Common.h"

#include "dsp/SampleFile.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace valis::elements {

using dsp::SampleExpectation;
using dsp::SampleFile;

class TimeStretch final : public DspElement
{
    static constexpr int   kOrder = 11;
    static constexpr int   kSize  = 1 << kOrder;   // 2048: 43 ms at 48 kHz
    static constexpr int   kHop   = kSize / 4;     // synthesis hop
    static constexpr int   kBins  = kSize / 2 + 1;
    static constexpr int   kRing  = 1 << 14;
    static constexpr int   kProbe = 128;           // samples between the two frames that give a first frequency
    static constexpr int   kPreroll = 3;           // frames before the first sample is read
    static constexpr double kTwoPi = 6.283185307179586;

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate   = rate;
        speedIndex   = controlIndex(type, "speed");
        pitchIndex   = controlIndex(type, "pitch");
        formantIndex = controlIndex(type, "formant");
        loopIndex    = controlIndex(type, "loop");
        startIndex   = controlIndex(type, "start");
        triggerIndex = controlIndex(type, "trigger");

        fft = std::make_unique<juce::dsp::FFT>(kOrder);

        window.resize(kSize);
        for (int i = 0; i < kSize; ++i)
            window[static_cast<std::size_t>(i)] =
                static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * i / kSize));

        double sum = 0.0;
        for (int offset = 0; offset < kSize; offset += kHop)
        {
            const double w = window[static_cast<std::size_t>(offset)];
            sum += w * w;
        }
        normalisation = sum > 0.0 ? static_cast<float>(1.0 / sum) : 1.0f;

        frame.assign(static_cast<std::size_t>(kSize) * 2, 0.0f);
        cepstrum.assign(static_cast<std::size_t>(kSize) * 2, 0.0f);
        ring.assign(kRing, 0.0f);
        for (auto* v : { &magnitude, &phase, &previousPhase, &synthPhase, &omega, &envelope })
            v->assign(kBins, 0.0f);
        peakOf.assign(kBins, 0);

        reset();
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error) override
    {
        if (const auto declared = expectation.setDeclared(key, value, error);
            declared != SampleExpectation::Declared::No)
        {
            return declared == SampleExpectation::Declared::Yes
                && expectation.check(source, error);
        }

        if (key != "file")
            return true;

        SampleFile file;
        if (! file.load(value, sampleRate, error))
            return false;

        source = file;
        if (! expectation.check(source, error))
            return false;

        samples = std::move(file.samples);
        reset();
        return true;
    }

    void reset() override
    {
        std::fill(ring.begin(), ring.end(), 0.0f);
        sourcePosition = 0.0;
        havePrevious   = false;
        previousAt     = 0;
        frameStart     = 0;
        readPosition   = 0.0;
        started        = false;
        ended          = false;
        endLimit       = 0;
        lastTrigger    = 0.0f;
        triggered      = false;
        playing        = ! samples.empty();
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        float* out = args.audioOut[0];
        const int n = args.numSamples;
        std::fill(out, out + n, 0.0f);

        const int length = static_cast<int>(samples.size());
        if (length <= 0)
            return;

        const float speed   = std::clamp(controlAt(args, speedIndex, 1.0f), 0.0f, 4.0f);
        const float pitch   = std::clamp(controlAt(args, pitchIndex, 0.0f), -24.0f, 24.0f);
        const bool  formant = controlAt(args, formantIndex, 0.0f) > 0.5f;
        looping = controlAt(args, loopIndex, 1.0f) > 0.5f;
        const float startCtl = std::clamp(controlAt(args, startIndex, 0.0f), 0.0f, 1.0f);
        const float trigger  = controlAt(args, triggerIndex, -1.0f);

        // As val:SampleLoad: a connected trigger owns playback, and each rise restarts it.
        bool restart = false;
        if (trigger >= -0.5f)
        {
            if (! triggered)
                playing = false;
            if (trigger > 0.5f && lastTrigger <= 0.5f)
            {
                restart   = true;
                triggered = true;
            }
            lastTrigger = trigger;
        }

        if (restart)
        {
            reset();
            lastTrigger = trigger;
            triggered   = true;
            playing     = true;
            sourcePosition = static_cast<double>(startCtl) * length;
        }
        else if (! started && playing)
            sourcePosition = static_cast<double>(startCtl) * length;

        if (! playing)
            return;

        ratio        = std::exp2(static_cast<double>(pitch) / 12.0);
        keepFormants = formant && std::abs(pitch) > 0.01f;

        // Source samples consumed per output sample is `speed`. The output is read `ratio`
        // samples at a time from a stream made `kHop` at a time, so each frame moves the
        // source on by speed * kHop / ratio. Beyond half a frame the phase between frames is
        // ambiguous, so that is the most a frame may move.
        sourceHop = std::min(static_cast<double>(speed) * kHop / ratio, kSize / 2.0);

        if (! started)
        {
            // The first samples of an overlap-add are only partly covered, so a few frames
            // are laid down on the first position, still, before the first sample is read.
            for (int i = 0; i < kPreroll + 1; ++i)
                synthesise(true);
            readPosition = static_cast<double>(kPreroll) * kHop;
            started = true;
        }

        for (int i = 0; i < n; ++i)
        {
            while (readPosition + 3.0 >= static_cast<double>(frameStart))
            {
                if (ended && readPosition > static_cast<double>(endLimit))
                    break;
                synthesise(false);
            }

            if (ended && readPosition > static_cast<double>(endLimit))
            {
                playing = false;
                break;
            }

            out[i] = interpolate(readPosition);
            readPosition += ratio;
        }
    }

private:
    float sampleAt(long index) const noexcept
    {
        const long length = static_cast<long>(samples.size());
        if (looping)
        {
            index %= length;
            if (index < 0) index += length;
            return samples[static_cast<std::size_t>(index)];
        }
        return index >= 0 && index < length ? samples[static_cast<std::size_t>(index)] : 0.0f;
    }

    /// Four-point cubic through the synthesised stream.
    float interpolate(double position) const noexcept
    {
        const auto whole = static_cast<long long>(std::floor(position));
        const float t = static_cast<float>(position - static_cast<double>(whole));
        const auto at = [this](long long i) { return ring[static_cast<std::size_t>(i & (kRing - 1))]; };

        const float y0 = at(whole - 1), y1 = at(whole), y2 = at(whole + 1), y3 = at(whole + 2);
        const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c = -0.5f * y0 + 0.5f * y2;
        return ((a * t + b) * t + c) * t + y1;
    }

    /// Analyses one frame of the file, advances every partial's phase for the synthesis hop
    /// and overlap-adds the result. `hold` analyses the same place again, which is how the
    /// first frames of a note are made.
    void synthesise(bool hold) noexcept
    {
        const long length = static_cast<long>(samples.size());
        long at = hold && havePrevious ? previousAt : std::lround(sourcePosition);

        long hop = havePrevious ? at - previousAt : 0;
        if (hop < 0 && looping)
            hop += length;
        if (hop < 0)
            hop = 0;

        // -- analysis --------------------------------------------------------------
        for (int i = 0; i < kSize; ++i)
            frame[static_cast<std::size_t>(i)] = sampleAt(at + i) * window[static_cast<std::size_t>(i)];
        std::fill(frame.begin() + kSize, frame.end(), 0.0f);
        fft->performRealOnlyForwardTransform(frame.data());

        for (int k = 0; k < kBins; ++k)
        {
            const float re = frame[static_cast<std::size_t>(2 * k)];
            const float im = frame[static_cast<std::size_t>(2 * k + 1)];
            magnitude[static_cast<std::size_t>(k)] = std::hypot(re, im);
            phase[static_cast<std::size_t>(k)]     = std::atan2(im, re);
        }

        // -- the frequency of each partial, from how its phase moved since the last frame
        if (! havePrevious)
        {
            // Nothing came before, so a second frame a little way on stands in for it. Without
            // it a frozen sound would sit on the nearest bin centre, up to half a bin out of tune.
            for (int i = 0; i < kSize; ++i)
                cepstrum[static_cast<std::size_t>(i)] = sampleAt(at + kProbe + i) * window[static_cast<std::size_t>(i)];
            std::fill(cepstrum.begin() + kSize, cepstrum.end(), 0.0f);
            fft->performRealOnlyForwardTransform(cepstrum.data());

            for (int k = 0; k < kBins; ++k)
            {
                const double later = std::atan2(cepstrum[static_cast<std::size_t>(2 * k + 1)],
                                                cepstrum[static_cast<std::size_t>(2 * k)]);
                const double deviation = later - static_cast<double>(phase[static_cast<std::size_t>(k)])
                                       - kTwoPi * k * kProbe / kSize;
                omega[static_cast<std::size_t>(k)] = static_cast<float>(
                    kTwoPi * k / kSize + std::remainder(deviation, kTwoPi) / kProbe);
                synthPhase[static_cast<std::size_t>(k)] = phase[static_cast<std::size_t>(k)];
            }
        }
        else if (hop > 0)
        {
            for (int k = 0; k < kBins; ++k)
            {
                const double expected = kTwoPi * k * static_cast<double>(hop) / kSize;
                const double deviation = static_cast<double>(phase[static_cast<std::size_t>(k)])
                                       - static_cast<double>(previousPhase[static_cast<std::size_t>(k)]) - expected;
                const double wrapped = std::remainder(deviation, kTwoPi);
                omega[static_cast<std::size_t>(k)] =
                    static_cast<float>(kTwoPi * k / kSize + wrapped / static_cast<double>(hop));
            }
        }
        // With no hop, the last frequencies stand.

        // -- peaks, and phase advance with locking around them ---------------------------
        int peaks = 0;
        for (int k = 0; k < kBins; ++k)
        {
            const float m = magnitude[static_cast<std::size_t>(k)];
            const auto neighbour = [&](int j)
            {
                return j >= 0 && j < kBins ? magnitude[static_cast<std::size_t>(j)] : 0.0f;
            };
            if (m > neighbour(k - 1) && m >= neighbour(k + 1) && m > neighbour(k - 2) && m >= neighbour(k + 2))
                peakOf[static_cast<std::size_t>(peaks++)] = k;
        }

        if (havePrevious)
        {
            if (peaks == 0)
            {
                for (int k = 0; k < kBins; ++k)
                    synthPhase[static_cast<std::size_t>(k)] = wrap(synthPhase[static_cast<std::size_t>(k)]
                        + omega[static_cast<std::size_t>(k)] * kHop);
            }
            else
            {
                // Each bin belongs to the nearest peak. The peaks move on by their own
                // frequency and the bins around them keep their phase relation to the peak.
                std::array<float, kBins> next;
                int region = 0;
                for (int k = 0; k < kBins; ++k)
                {
                    while (region + 1 < peaks
                           && k - peakOf[static_cast<std::size_t>(region)] > peakOf[static_cast<std::size_t>(region + 1)] - k)
                        ++region;

                    const int p = peakOf[static_cast<std::size_t>(region)];
                    const float peakNext = wrap(synthPhase[static_cast<std::size_t>(p)]
                                                + omega[static_cast<std::size_t>(p)] * kHop);
                    next[static_cast<std::size_t>(k)] = wrap(peakNext + phase[static_cast<std::size_t>(k)]
                                                             - phase[static_cast<std::size_t>(p)]);
                }
                for (int k = 0; k < kBins; ++k)
                    synthPhase[static_cast<std::size_t>(k)] = next[static_cast<std::size_t>(k)];
            }
        }

        // -- formants ----------------------------------------------------------------
        if (keepFormants)
            smoothEnvelope();

        // -- synthesis ---------------------------------------------------------------
        for (int k = 0; k < kBins; ++k)
        {
            float m = magnitude[static_cast<std::size_t>(k)];

            if (keepFormants)
            {
                const double landing = std::min(static_cast<double>(k) * ratio, static_cast<double>(kBins - 1));
                const auto whole = static_cast<int>(landing);
                const double frac = landing - whole;
                const float there = envelope[static_cast<std::size_t>(whole)] * static_cast<float>(1.0 - frac)
                                  + envelope[static_cast<std::size_t>(std::min(whole + 1, kBins - 1))] * static_cast<float>(frac);
                m *= std::clamp(there / std::max(envelope[static_cast<std::size_t>(k)], 1.0e-9f), 0.05f, 20.0f);
            }

            if (k == 0 || k == kSize / 2)
            {
                // The ends of a real spectrum have no phase to move.
                frame[static_cast<std::size_t>(2 * k)]     = m * (std::cos(phase[static_cast<std::size_t>(k)]) >= 0.0f ? 1.0f : -1.0f);
                frame[static_cast<std::size_t>(2 * k + 1)] = 0.0f;
                continue;
            }

            const float p = synthPhase[static_cast<std::size_t>(k)];
            frame[static_cast<std::size_t>(2 * k)]     = m * std::cos(p);
            frame[static_cast<std::size_t>(2 * k + 1)] = m * std::sin(p);
        }
        fft->performRealOnlyInverseTransform(frame.data());

        // Only the last hop of the frame lands on ground nothing has written yet.
        for (int i = kSize - kHop; i < kSize; ++i)
            ring[static_cast<std::size_t>((frameStart + i) & (kRing - 1))] = 0.0f;
        for (int i = 0; i < kSize; ++i)
            ring[static_cast<std::size_t>((frameStart + i) & (kRing - 1))] +=
                frame[static_cast<std::size_t>(i)] * window[static_cast<std::size_t>(i)] * normalisation;

        // -- bookkeeping -------------------------------------------------------------
        std::copy(phase.begin(), phase.end(), previousPhase.begin());
        previousAt    = at;
        havePrevious  = true;
        frameStart   += kHop;

        if (! hold)
        {
            sourcePosition += sourceHop;
            if (looping)
            {
                while (sourcePosition >= static_cast<double>(length))
                    sourcePosition -= static_cast<double>(length);
            }
            else if (! ended && sourcePosition >= static_cast<double>(length))
            {
                // The file is over. What is left in the stream is the last frame's tail.
                ended    = true;
                endLimit = frameStart + kSize;
            }
        }
    }

    /// The smoothed spectral envelope of the current frame: the log magnitude, its
    /// cepstrum with only the slow part kept, and back.
    void smoothEnvelope() noexcept
    {
        std::fill(cepstrum.begin(), cepstrum.end(), 0.0f);
        for (int k = 0; k < kBins; ++k)
        {
            const float logMagnitude = std::log(magnitude[static_cast<std::size_t>(k)] + 1.0e-6f);
            cepstrum[static_cast<std::size_t>(k)] = logMagnitude;
            if (k > 0 && k < kSize / 2)
                cepstrum[static_cast<std::size_t>(kSize - k)] = logMagnitude;
        }
        fft->performRealOnlyForwardTransform(cepstrum.data());

        // The log spectrum of a real signal is real and even, and so is its cepstrum. Keep
        // quefrencies up to about two milliseconds, which is the shape of the vocal tract and
        // not the spacing of the harmonics, tapered to nothing.
        const int keep = std::max(8, static_cast<int>(sampleRate / 500.0));
        std::array<float, kSize> lifted{};
        for (int n = 0; n < kSize; ++n)
        {
            const int distance = std::min(n, kSize - n);
            const float taper = distance >= keep ? 0.0f
                              : 0.5f + 0.5f * std::cos(static_cast<float>(3.14159265358979 * distance / keep));
            lifted[static_cast<std::size_t>(n)] = cepstrum[static_cast<std::size_t>(2 * n)] * (1.0f / kSize) * taper;
        }

        std::fill(cepstrum.begin(), cepstrum.end(), 0.0f);
        for (int n = 0; n < kSize; ++n)
            cepstrum[static_cast<std::size_t>(n)] = lifted[static_cast<std::size_t>(n)];
        fft->performRealOnlyForwardTransform(cepstrum.data());

        for (int k = 0; k < kBins; ++k)
            envelope[static_cast<std::size_t>(k)] = std::exp(cepstrum[static_cast<std::size_t>(2 * k)]);
    }

    static float wrap(float angle) noexcept
    {
        return std::remainder(angle, static_cast<float>(kTwoPi));
    }

    SampleExpectation expectation;
    SampleFile        source;
    std::vector<float> samples;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, frame, cepstrum, ring;
    std::vector<float> magnitude, phase, previousPhase, synthPhase, omega, envelope;
    std::vector<int>   peakOf;
    float normalisation = 1.0f;

    double sampleRate = 48000.0;
    double sourcePosition = 0.0, sourceHop = 0.0, ratio = 1.0, readPosition = 0.0;
    long long frameStart = 0, endLimit = 0;
    long previousAt = 0;
    bool havePrevious = false, started = false, ended = false, playing = false;
    bool triggered = false, looping = true, keepFormants = false;
    float lastTrigger = 0.0f;
    int speedIndex = -1, pitchIndex = -1, formantIndex = -1, loopIndex = -1,
        startIndex = -1, triggerIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerTimeStretch(ElementRegistry& registry)
{
    registry.add("TimeStretch", &make<elements::TimeStretch>);
}

}  // namespace valis

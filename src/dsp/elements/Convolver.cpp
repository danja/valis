// src/dsp/elements/Convolver.cpp
//
// val:Convolver convolves its input with an impulse response read from a sound file: the
// sound of a room, a cabinet or a spring, captured by playing an impulse (or a sine sweep)
// through it and recording the result.
// https://en.wikipedia.org/wiki/Convolution_reverb
//
// A direct convolution costs one multiply per sample of response for every sample of
// input, which is far too much for a response of seconds. The response is cut into
// partitions of kBlock samples and each is transformed once, on the message thread. At run
// time the input is transformed a block at a time, the spectra of the last few blocks are
// kept in a delay line, and the output block is the sum of each block's spectrum times the
// matching partition's. This is uniformly partitioned overlap-save convolution; see
// Wefers, "Partitioned convolution algorithms for real-time auralization" (2015).
//
// A block-at-a-time transform delays the output by a block. To add no latency the first
// partition is not done that way: it is a direct filter on the input samples, run every
// sample, and the transform handles only partitions from the second on. The output of
// block m needs the input of blocks up to m-1 for those, and that is all complete when
// block m starts. The rest of the work is a bounded number of multiplies per block, with no
// allocation.
//
// The response is summed to mono and resampled to the engine's rate, as for val:SampleLoad,
// then scaled to unit energy so that a response recorded loud and one recorded quiet give a
// wet signal of about the level of the input. val:gain trims that. A response longer than
// kMaxSeconds is cut there, with a short fade so it does not end in a click. With no file
// loaded the input passes through unchanged, which is what an impulse of 1 does.

#include "Common.h"

#include "dsp/SampleFile.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <memory>
#include <vector>

namespace valis::elements {

using dsp::SampleExpectation;
using dsp::SampleFile;

class Convolver final : public DspElement
{
    static constexpr int kOrder      = 8;
    static constexpr int kFftSize    = 1 << kOrder;
    static constexpr int kBlock      = kFftSize / 2;   // samples per partition
    static constexpr int kBins       = kBlock + 1;
    static constexpr double kMaxSeconds = 12.0;
    static constexpr double kFadeSeconds = 0.05;

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        mixIndex   = controlIndex(type, "mix");
        gainIndex  = controlIndex(type, "gain");
        glide      = timeToCoeff(10.0f, rate);

        fft = std::make_unique<juce::dsp::FFT>(kOrder);

        // The inverse transform's scaling differs between JUCE's back ends, so it is measured:
        // an impulse taken there and back comes out as the factor to undo.
        scratch.assign(static_cast<std::size_t>(kFftSize) * 2, 0.0f);
        scratch[0] = 1.0f;
        fft->performRealOnlyForwardTransform(scratch.data());
        fft->performRealOnlyInverseTransform(scratch.data());
        inverseScale = std::abs(scratch[0]) > 1.0e-9f ? 1.0f / scratch[0] : 1.0f;

        scratch.assign(static_cast<std::size_t>(kFftSize) * 2, 0.0f);
        accumulator.assign(static_cast<std::size_t>(kFftSize) * 2, 0.0f);
        history.assign(static_cast<std::size_t>(kBlock) * 2, 0.0f);
        pending.assign(kFftSize, 0.0f);
        tail.assign(kBlock, 0.0f);

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

        installResponse(file.samples);
        reset();
        return true;
    }

    void reset() override
    {
        std::fill(history.begin(), history.end(), 0.0f);
        std::fill(pending.begin(), pending.end(), 0.0f);
        std::fill(tail.begin(), tail.end(), 0.0f);
        std::fill(spectra.begin(), spectra.end(), 0.0f);
        position = 0;
        newest   = 0;
        mixGlide.reset();
        gainGlide.reset();
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];
        const int    n   = args.numSamples;

        const float mix  = std::clamp(controlAt(args, mixIndex, 1.0f), 0.0f, 1.0f);
        const float gain = dbToGain(std::clamp(controlAt(args, gainIndex, 0.0f), -48.0f, 24.0f));

        if (! hasResponse)
        {
            std::copy(in, in + n, out);
            return;
        }

        const float* head = reversedHead.data();
        float* const hist = history.data();

        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];

            // Two copies of the last block of input, so the direct filter reads it straight.
            hist[position]          = x;
            hist[position + kBlock] = x;

            const float* recent = hist + position + 1;
            float direct = 0.0f;
            for (int j = 0; j < kBlock; ++j)
                direct += head[j] * recent[j];

            const float wet = (direct + tail[static_cast<std::size_t>(position)]) * gainGlide.step(gain, glide);
            const float m   = mixGlide.step(mix, glide);
            out[i] = x * (1.0f - m) + wet * m;

            pending[static_cast<std::size_t>(kBlock + position)] = x;

            if (++position == kBlock)
            {
                position = 0;
                endOfBlock();
            }
        }
    }

private:
    /// Cuts the response into partitions and takes each to the frequency domain. Message thread.
    void installResponse(const std::vector<float>& file)
    {
        const auto limit = static_cast<std::size_t>(kMaxSeconds * sampleRate);
        std::vector<float> response(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(std::min(file.size(), limit)));

        if (file.size() > limit)
        {
            const auto fade = std::min(response.size(), static_cast<std::size_t>(kFadeSeconds * sampleRate));
            for (std::size_t i = 0; i < fade; ++i)
                response[response.size() - fade + i] *= 1.0f - static_cast<float>(i + 1) / static_cast<float>(fade);
        }

        double energy = 0.0;
        for (const float s : response)
            energy += static_cast<double>(s) * s;

        if (energy <= 0.0)
        {
            hasResponse = false;   // a silent file is not a response; the input passes
            return;
        }

        const auto scale = static_cast<float>(1.0 / std::sqrt(energy));
        for (auto& s : response)
            s *= scale;

        const auto partitions = (response.size() + kBlock - 1) / kBlock;
        response.resize(partitions * kBlock, 0.0f);

        reversedHead.assign(kBlock, 0.0f);
        for (int j = 0; j < kBlock; ++j)
            reversedHead[static_cast<std::size_t>(kBlock - 1 - j)] = response[static_cast<std::size_t>(j)];

        later = static_cast<int>(partitions) - 1;
        partitionSpectra.assign(static_cast<std::size_t>(later) * kBins * 2, 0.0f);
        spectra.assign(static_cast<std::size_t>(later) * kBins * 2, 0.0f);

        for (int k = 1; k <= later; ++k)
        {
            std::fill(scratch.begin(), scratch.end(), 0.0f);
            std::copy(response.begin() + static_cast<std::ptrdiff_t>(k) * kBlock,
                      response.begin() + static_cast<std::ptrdiff_t>(k + 1) * kBlock, scratch.begin());
            fft->performRealOnlyForwardTransform(scratch.data());
            std::copy(scratch.begin(), scratch.begin() + kBins * 2,
                      partitionSpectra.begin() + static_cast<std::ptrdiff_t>(k - 1) * kBins * 2);
        }

        hasResponse = true;
    }

    /// Called when a block of input is complete: transforms it with the one before, and sums
    /// the products with the partitions into the tail that the next block will add.
    void endOfBlock() noexcept
    {
        std::fill(tail.begin(), tail.end(), 0.0f);
        if (later <= 0)
            return;

        // The last two blocks of input, the older first, are one overlap-save frame.
        std::copy(pending.begin(), pending.end(), scratch.begin());
        std::fill(scratch.begin() + kFftSize, scratch.end(), 0.0f);
        fft->performRealOnlyForwardTransform(scratch.data());

        newest = newest == 0 ? later - 1 : newest - 1;
        std::copy(scratch.begin(), scratch.begin() + kBins * 2,
                  spectra.begin() + static_cast<std::ptrdiff_t>(newest) * kBins * 2);

        std::fill(accumulator.begin(), accumulator.end(), 0.0f);
        for (int k = 1; k <= later; ++k)
        {
            // The block k back is k - 1 slots past the newest, and meets partition k.
            const int slot = (newest + k - 1) % later;
            const float* x = spectra.data() + static_cast<std::size_t>(slot) * kBins * 2;
            const float* h = partitionSpectra.data() + static_cast<std::size_t>(k - 1) * kBins * 2;
            float* sum = accumulator.data();

            for (int b = 0; b < kBins; ++b)
            {
                const float xr = x[2 * b], xi = x[2 * b + 1];
                const float hr = h[2 * b], hi = h[2 * b + 1];
                sum[2 * b]     += xr * hr - xi * hi;
                sum[2 * b + 1] += xr * hi + xi * hr;
            }
        }

        fft->performRealOnlyInverseTransform(accumulator.data());
        for (int i = 0; i < kBlock; ++i)
            tail[static_cast<std::size_t>(i)] = accumulator[static_cast<std::size_t>(kBlock + i)] * inverseScale;

        // The newer block becomes the older one for the next frame.
        std::copy(pending.begin() + kBlock, pending.end(), pending.begin());
    }

    SampleExpectation expectation;
    SampleFile        source;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> reversedHead;       ///< the first partition, last sample first
    std::vector<float> partitionSpectra;   ///< partitions 1 and on, kBins complex numbers each
    std::vector<float> spectra;            ///< a ring of the input spectra, newest at `newest`
    std::vector<float> history, pending, tail, scratch, accumulator;

    float  inverseScale = 1.0f;
    float  glide = 0.0f;
    Smoothed mixGlide, gainGlide;
    double sampleRate = 48000.0;
    int    position = 0, newest = 0, later = 0;
    bool   hasResponse = false;
    int    mixIndex = -1, gainIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerConvolver(ElementRegistry& registry)
{
    registry.add("Convolver", &make<elements::Convolver>);
}

}  // namespace valis

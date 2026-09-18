// src/dsp/Oversampled.h
//
// Runs one element at a multiple of the sample rate.
//
// A nonlinearity generates harmonics above the ones it was given. Any that land
// above Nyquist fold back down as inharmonic tones that were never in the
// signal. val:antialiasing answers that for a memoryless curve by integrating
// it; this answers it the other way, by giving the element more room before the
// fold-back happens and filtering the result on the way back down.
//
// A decorator rather than a base class: the element inside knows nothing about
// it, so any element can be oversampled, and an element written later needs no
// change to take part.
//
// Message thread for prepare() and setOption(); process() allocates nothing.

#pragma once

#include "valis/DspElement.h"
#include "valis/Ontology.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace valis {

class Oversampled final : public DspElement
{
public:
    /// Reads val:oversampling. Returns false and fills `error` when the value is
    /// not one of the factors, so a typo fails the load rather than quietly
    /// running at the base rate.
    static bool parseFactor(std::string_view value, int& factor, std::string& error)
    {
        const auto text = std::string(value);
        const int  n    = std::atoi(text.c_str());

        if (n == 1 || n == 2 || n == 4 || n == 8 || n == 16)
        {
            factor = n;
            return true;
        }

        error = "val:oversampling must be 1, 2, 4, 8 or 16, not '" + text + "'";
        return false;
    }

    Oversampled(std::unique_ptr<DspElement> element, int oversamplingFactor)
        : inner(std::move(element)), factor(oversamplingFactor)
    {
    }

    void prepare(const ElementType& type, double sampleRate, int maxBlockSize) override
    {
        numAudioIn  = type.countPorts(true,  false);
        numAudioOut = type.countPorts(false, false);

        // One oversampling object carries both directions, so it needs room for
        // whichever side is wider.
        channels  = static_cast<std::size_t>(std::max({numAudioIn, numAudioOut, 1}));
        blockSize = std::max(maxBlockSize, 1);

        const auto stages = static_cast<std::size_t>(std::lround(std::log2(static_cast<double>(factor))));

        // Polyphase IIR: the usual choice around a nonlinearity, because it
        // costs a fraction of the linear-phase filter's latency. The phase is
        // not flat, which matters for a parallel dry path and not for a
        // saturator in series.
        oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
            channels, stages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
        oversampling->initProcessing(static_cast<std::size_t>(blockSize));

        const auto wide = static_cast<std::size_t>(blockSize) * static_cast<std::size_t>(factor);

        upstream.assign(channels * static_cast<std::size_t>(blockSize), 0.0f);
        innerOut.assign(channels * wide, 0.0f);
        downstream.assign(channels * static_cast<std::size_t>(blockSize), 0.0f);
        wideSilence.assign(wide, 0.0f);

        upPtrs.assign(channels, nullptr);
        upConstPtrs.assign(channels, nullptr);
        downPtrs.assign(channels, nullptr);
        innerIn.assign(channels, nullptr);
        innerOutPtrs.assign(channels, nullptr);

        for (std::size_t c = 0; c < channels; ++c)
        {
            upPtrs[c]       = upstream.data() + c * static_cast<std::size_t>(blockSize);
            upConstPtrs[c]  = upPtrs[c];
            downPtrs[c]     = downstream.data() + c * static_cast<std::size_t>(blockSize);
            innerOutPtrs[c] = innerOut.data() + c * wide;
        }

        // The element inside runs in a faster world, and is told so.
        inner->prepare(type, sampleRate * factor, blockSize * factor);
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error) override
    {
        // val:oversampling was consumed before this wrapper was built.
        if (key == "oversampling")
            return true;

        return inner->setOption(key, value, error);
    }

    void reset() override
    {
        inner->reset();
        if (oversampling != nullptr)
            oversampling->reset();

        std::fill(upstream.begin(), upstream.end(), 0.0f);
        std::fill(innerOut.begin(), innerOut.end(), 0.0f);
        std::fill(downstream.begin(), downstream.end(), 0.0f);
    }

    /// The element's own latency is counted in the faster world, so it is worth
    /// that many fewer samples here. The resampling filters add their own.
    int latencyInSamples() const override
    {
        const int innerLatency = inner->latencyInSamples() / factor;
        const int filters = oversampling != nullptr
                          ? static_cast<int>(std::lround(oversampling->getLatencyInSamples()))
                          : 0;
        return innerLatency + filters;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (oversampling == nullptr || args.numSamples > blockSize)
            return;

        const auto n    = static_cast<std::size_t>(args.numSamples);
        const auto wide = n * static_cast<std::size_t>(factor);

        // Gather the element's audio inputs, zero-padding the channels the
        // oversampler has but this element does not use.
        for (std::size_t c = 0; c < channels; ++c)
        {
            const bool live = static_cast<int>(c) < args.numAudioIn
                           && args.audioIn != nullptr
                           && args.audioIn[c] != nullptr;

            if (live)
                std::copy(args.audioIn[c], args.audioIn[c] + n, upPtrs[c]);
            else
                std::fill(upPtrs[c], upPtrs[c] + n, 0.0f);
        }

        juce::dsp::AudioBlock<const float> inputBlock(upConstPtrs.data(), channels, n);
        auto osBlock = oversampling->processSamplesUp(inputBlock);

        // An element that distinguishes "nothing is wired here" from "what is
        // wired here is quiet" compares its input against the silence block, so
        // that distinction has to survive the resampling: an unconnected input
        // is handed the wide silence rather than an upsampled copy of it.
        for (std::size_t c = 0; c < channels; ++c)
        {
            const bool unconnected = args.silence != nullptr
                                  && static_cast<int>(c) < args.numAudioIn
                                  && args.audioIn != nullptr
                                  && args.audioIn[c] == args.silence;

            innerIn[c] = unconnected ? wideSilence.data() : osBlock.getChannelPointer(c);
        }

        ProcessArgs wideArgs   = args;
        wideArgs.audioIn       = innerIn.data();
        wideArgs.audioOut      = innerOutPtrs.data();
        wideArgs.silence       = wideSilence.data();
        wideArgs.numSamples    = static_cast<int>(wide);

        inner->process(wideArgs);

        // processSamplesDown reads back out of the oversampler's own buffer, so
        // what the element produced has to be put there first.
        for (int c = 0; c < numAudioOut && static_cast<std::size_t>(c) < channels; ++c)
        {
            float* destination = osBlock.getChannelPointer(static_cast<std::size_t>(c));
            const float* source = innerOutPtrs[static_cast<std::size_t>(c)];
            std::copy(source, source + wide, destination);
        }

        const auto outChannels = static_cast<std::size_t>(std::max(numAudioOut, 0));
        if (outChannels == 0)
            return;

        juce::dsp::AudioBlock<float> outputBlock(downPtrs.data(), outChannels, n);
        oversampling->processSamplesDown(outputBlock);

        for (int c = 0; c < numAudioOut && c < args.numAudioOut; ++c)
        {
            const auto index = static_cast<std::size_t>(c);
            std::copy(downPtrs[index], downPtrs[index] + n, args.audioOut[c]);
        }
    }

private:
    std::unique_ptr<DspElement> inner;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    int factor      = 1;
    int blockSize   = 0;
    int numAudioIn  = 0;
    int numAudioOut = 0;
    std::size_t channels = 1;

    /// Preallocated in prepare(). `innerOut` and `wideSilence` are at the
    /// oversampled rate; the others are at the host's.
    std::vector<float> upstream, innerOut, downstream, wideSilence;
    std::vector<float*> upPtrs, downPtrs, innerOutPtrs;

    /// A const view of `upPtrs`, because an input AudioBlock wants one and the
    /// gather above needs to write through the same pointers.
    std::vector<const float*> upConstPtrs;
    std::vector<const float*> innerIn;
};

}  // namespace valis

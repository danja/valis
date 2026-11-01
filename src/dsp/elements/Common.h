// src/dsp/elements/Common.h
//
// Shared scaffolding for the element implementations. Everything here is called
// from process(), so nothing allocates.

#pragma once

#include "valis/DspElement.h"
#include "valis/Ontology.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace valis::elements {

inline float controlAt(const ProcessArgs& args, int index, float fallback = 0.0f) noexcept
{
    return index >= 0 && index < args.numControlIn ? args.controlIn[index] : fallback;
}

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) noexcept  { return 20.0f * std::log10(std::max(g, 1.0e-9f)); }

/// One-pole smoothing coefficient for a time constant in milliseconds.
inline float timeToCoeff(float ms, double sampleRate) noexcept
{
    if (ms <= 0.0f)
        return 0.0f;

    return static_cast<float>(std::exp(-1.0 / (0.001 * ms * sampleRate)));
}

/// A control value glided to its target one sample at a time, so a knob that jumps does not click. The first
/// value after a reset is taken as it is, so an element does not start by gliding up from zero. The glide
/// runs per sample, so the sound does not depend on how the host cut the stream.
struct Smoothed
{
    float value = 0.0f;
    bool  primed = false;

    void reset() noexcept { primed = false; }

    float step(float target, float coefficient) noexcept
    {
        if (! primed)
        {
            value = target;
            primed = true;
        }
        value = target + coefficient * (value - target);
        return value;
    }
};

/// A single-input, single-output element. Most of the library is this shape.
class MonoElement : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        cacheIndices(type);
        reset();
    }

    void process(const ProcessArgs& args) noexcept final
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        processMono(args.audioIn[0], args.audioOut[0], args.numSamples, args);
    }

protected:
    virtual void processMono(const float* in, float* out, int n, const ProcessArgs&) noexcept = 0;
    virtual void cacheIndices(const ElementType&) {}

    double sampleRate = 44100.0;
};

/// Index of a control input by symbol, or -1. Called in prepare(), never in
/// process(), so the string comparison is free at run time.
inline int controlIndex(const ElementType& type, std::string_view symbol)
{
    int index = 0;
    for (const auto& port : type.ports)
    {
        if (! port.input || ! port.control)
            continue;

        if (port.symbol == symbol)
            return index;
        ++index;
    }
    return -1;
}

inline int audioOutIndex(const ElementType& type, std::string_view symbol)
{
    int index = 0;
    for (const auto& port : type.ports)
    {
        if (port.input || port.control)
            continue;

        if (port.symbol == symbol)
            return index;
        ++index;
    }
    return -1;
}

inline int audioInIndex(const ElementType& type, std::string_view symbol)
{
    int index = 0;
    for (const auto& port : type.ports)
    {
        if (! port.input || port.control)
            continue;

        if (port.symbol == symbol)
            return index;
        ++index;
    }
    return -1;
}

}  // namespace valis::elements

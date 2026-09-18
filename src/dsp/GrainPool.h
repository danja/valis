// src/dsp/GrainPool.h
//
// A bounded population of grains.
//
// This is the pattern the plugin survey calls a pool: a fixed number of short
// lived things, allocated and retired deterministically, with nothing on the
// heap. Voices are the same shape and are handled by the engine; grains are
// small enough and numerous enough to belong to whichever element is making
// them, so they live here instead.
//
// The pool owns the grains, their windows and their placement. It does not own
// the material they read: the caller passes a reader, because what a grain
// reads from is the interesting difference between a granulator, a granular
// reverb and a particle synthesiser, and it is the one thing that should not be
// baked in here.

#pragma once

#include "dsp/Random.h"

#include <array>
#include <cmath>

namespace valis::dsp {

/// Tukey window: `fade` is the fraction of the grain spent rising or falling.
/// At the smallest fade it is nearly rectangular, keeping the source transient
/// intact; at 0.5 the whole grain is one rise and fall and the grain boundary
/// is inaudible.
///
/// The raised cosine is approximated by the cubic 3t^2 - 2t^3, which agrees
/// with it to within 0.02 and has the same zero slope at both ends. This runs
/// once per grain per sample, so a cosine call here would dominate the cost.
inline float grainWindow(float phase, float fade) noexcept
{
    float t = 1.0f;

    if (phase < fade)
        t = phase / fade;
    else if (phase > 1.0f - fade)
        t = (1.0f - phase) / fade;
    else
        return 1.0f;

    return t * t * (3.0f - 2.0f * t);
}

/// What a grain is while it is sounding.
struct Grain
{
    bool   active    = false;
    double readPos   = 0.0;   ///< where in the material it is reading
    double increment = 1.0;   ///< samples per sample, negative to play backwards
    float  phase     = 0.0f;  ///< 0 to 1 across the grain's life
    float  phaseInc  = 0.0f;
    float  panL      = 0.70710678f;
    float  panR      = 0.70710678f;
};

/// How a grain is to be started. Grouped rather than passed as seven arguments,
/// because every one of them is a randomised spread around a centre and they
/// are set together.
struct GrainSpec
{
    double span        = 0.0;   ///< length of the material, in samples
    double origin      = 0.0;   ///< where position 0 sits in it
    float  position    = 0.0f;  ///< 0 to 1 through the material
    float  lengthSamples = 480.0f;
    float  pitch       = 0.0f;  ///< semitones
    float  spray       = 0.0f;  ///< randomises position, as a fraction of span
    float  pitchJitter = 0.0f;  ///< semitones, randomised either way
    float  spread      = 0.0f;  ///< stereo width, 0 centred
    float  reverse     = 0.0f;  ///< probability of playing backwards
};

/// `Capacity` grains at most. When they are all busy an onset is dropped: the
/// alternative on this thread is to allocate, and a texture missing one grain in
/// a cloud of sixty-four is inaudible.
template <int Capacity>
class GrainPool
{
public:
    static constexpr int capacity = Capacity;

    void clear() noexcept
    {
        for (auto& grain : grains)
            grain = Grain{};
    }

    /// The generator is the pool's, so an element holding one gets reproducible
    /// grains without keeping its own.
    void seed(std::uint32_t value) noexcept { random.seed(value); }

    int activeCount() const noexcept
    {
        int count = 0;
        for (const auto& grain : grains)
            count += grain.active ? 1 : 0;
        return count;
    }

    /// Starts one grain. Returns false when every grain is busy, or when there
    /// is no material to read.
    bool spawn(const GrainSpec& spec) noexcept
    {
        if (spec.span <= 0.0 || spec.lengthSamples <= 0.0f)
            return false;

        Grain* slot = nullptr;
        for (auto& grain : grains)
        {
            if (! grain.active)
            {
                slot = &grain;
                break;
            }
        }

        if (slot == nullptr)
            return false;

        double start = spec.origin
                     + static_cast<double>(spec.position) * spec.span
                     + static_cast<double>(spec.spray * random.bipolar()) * spec.span;

        while (start >= spec.span) start -= spec.span;
        while (start < 0.0)        start += spec.span;

        const float semitones = spec.pitch + spec.pitchJitter * random.bipolar();
        double increment = std::pow(2.0, static_cast<double>(semitones) / 12.0);
        if (random.chance(spec.reverse))
            increment = -increment;

        // Equal-power placement around the centre, widened by spread.
        const float pan   = 0.5f + 0.5f * spec.spread * random.bipolar();
        const float angle = pan * 1.5707963267948966f;

        slot->active    = true;
        slot->readPos   = start;
        slot->increment = increment;
        slot->phase     = 0.0f;
        slot->phaseInc  = 1.0f / spec.lengthSamples;
        slot->panL      = std::cos(angle);
        slot->panR      = std::sin(angle);
        return true;
    }

    /// Advances every sounding grain by one sample and adds what it contributes
    /// to `left` and `right`. `read` is handed a position in the material and
    /// returns the sample there; `span` is where the material wraps.
    ///
    /// A grain retires when its window runs out, which is the only way one ends.
    template <typename Read>
    void mix(float& left, float& right, float fade, double span, Read&& read) noexcept
    {
        for (auto& grain : grains)
        {
            if (! grain.active)
                continue;

            const float value = read(grain.readPos) * grainWindow(grain.phase, fade);

            left  += value * grain.panL;
            right += value * grain.panR;

            grain.readPos += grain.increment;
            while (grain.readPos >= span) grain.readPos -= span;
            while (grain.readPos < 0.0)   grain.readPos += span;

            grain.phase += grain.phaseInc;
            if (grain.phase >= 1.0f)
                grain.active = false;
        }
    }

    Random& generator() noexcept { return random; }

private:
    std::array<Grain, Capacity> grains{};
    Random random{0x5eed1234u};
};

}  // namespace valis::dsp

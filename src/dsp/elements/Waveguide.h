// src/dsp/elements/Waveguide.h
//
// The parts a digital waveguide instrument is built from. A wind instrument is
// a delay line standing for the air column, a filter standing for what the
// column loses on each round trip, and a nonlinear exciter standing for the
// reed or the jet. The first two are the same for every such instrument and
// live here; only the exciter differs, and that is what each element writes.
//
// Everything here is called from process(), so nothing allocates outside
// prepare().

#pragma once

#include "dsp/Random.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace valis::elements::waveguide {

/// A delay line read at a fractional position, by linear interpolation.
///
/// The air column is rarely a whole number of samples long, and rounding it
/// puts the instrument as much as a semitone out of tune in its top octave, so
/// the fraction is not optional.
class Delay
{
public:
    void prepare(std::size_t capacity)
    {
        line.assign(std::max<std::size_t>(capacity, 4), 0.0f);
        clear();
    }

    void clear()
    {
        std::fill(line.begin(), line.end(), 0.0f);
        writePos = 0;
    }

    void setDelay(float samples)
    {
        delay = std::clamp(samples, 1.0f, static_cast<float>(line.size()) - 2.0f);
    }

    float delayInSamples() const noexcept { return delay; }

    /// What is currently at the read point, without advancing.
    float last() const noexcept
    {
        const int size  = static_cast<int>(line.size());
        const int whole = static_cast<int>(delay);
        const float fraction = delay - static_cast<float>(whole);

        int a = writePos - whole;  if (a < 0) a += size;
        int b = a - 1;             if (b < 0) b += size;

        return line[static_cast<std::size_t>(a)] * (1.0f - fraction)
             + line[static_cast<std::size_t>(b)] * fraction;
    }

    float tick(float input) noexcept
    {
        const float output = last();
        line[static_cast<std::size_t>(writePos)] = input;
        if (++writePos >= static_cast<int>(line.size()))
            writePos = 0;
        return output;
    }

private:
    std::vector<float> line;
    int   writePos = 0;
    float delay    = 1.0f;
};

/// The air column's loss, as a one-pole lowpass. A tube loses more of a high
/// partial than a low one on every round trip, which is why an instrument's
/// upper harmonics fade first and why a soft wooden bore sounds darker than a
/// metal one.
class Loss
{
public:
    void clear() noexcept { state = 0.0f; }

    void setPole(float p) noexcept { pole = std::clamp(p, 0.0f, 0.98f); }
    float getPole() const noexcept { return pole; }

    float process(float input) noexcept
    {
        state = pole * state + (1.0f - pole) * input;
        return state;
    }

    /// How many samples of delay the filter adds at this frequency. A loop is
    /// tuned by its total delay, and this is the part of it that moves when the
    /// pole moves, so an instrument that does not account for it goes flat as
    /// it is damped.
    float phaseDelay(float omega) const noexcept
    {
        if (omega <= 0.0f)
            return pole / std::max(1.0f - pole, 1.0e-6f);

        return std::atan2(pole * std::sin(omega), 1.0f - pole * std::cos(omega)) / omega;
    }

private:
    float pole  = 0.5f;
    float state = 0.0f;
};

/// Removes the steady component a nonlinear exciter leaves behind. Without it
/// the working point drifts to one side and the exciter clips against one of
/// its limits instead of swinging about the middle.
class DcBlocker
{
public:
    void clear() noexcept { state = 0.0f; previous = 0.0f; }

    float process(float input) noexcept
    {
        const float output = input - previous + coefficient * state;
        previous = input;
        state    = output;
        return output;
    }

private:
    static constexpr float coefficient = 0.995f;
    float state = 0.0f, previous = 0.0f;
};

/// Pade approximation of tanh, within about 1e-3 over the range an exciter
/// uses. The real one, called once per sample per instrument, would be the most
/// expensive thing in the loop by a wide margin.
inline float fastTanh(float x) noexcept
{
    const float clamped = std::clamp(x, -4.0f, 4.0f);
    const float squared = clamped * clamped;
    return clamped * (27.0f + squared) / (27.0f + 9.0f * squared);
}

/// The turbulence in a player's breath, which is the shared deterministic
/// generator under another name: an exciter wants it bipolar and wants it the
/// same every run, and that is exactly what dsp::Random gives.
using Turbulence = dsp::Random;


}  // namespace valis::elements::waveguide

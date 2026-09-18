// src/dsp/Random.h
//
// One deterministic random source for the whole project.
//
// Every element that needs randomness needs the same three things: no
// allocation, no shared state between instances, and the same sequence every
// run. The last one is not a nicety. A render that changed from one run to the
// next could not be measured, and every audio test here works by measuring a
// render.
//
// An element seeds this in reset(), so relocating the transport puts it back
// where it started rather than leaving it wherever the last block left it.

#pragma once

#include <cstdint>

namespace valis::dsp {

/// Marsaglia's xorshift32. Cheap enough to call once per sample per voice, and
/// far better distributed than the linear congruential generator that usually
/// turns up in audio code.
///
/// https://en.wikipedia.org/wiki/Xorshift
class Random
{
public:
    Random() = default;
    explicit Random(std::uint32_t initial) noexcept { seed(initial); }

    /// Zero is the generator's fixed point, so it is replaced rather than
    /// accepted: a zero seed would produce silence for ever.
    void seed(std::uint32_t value) noexcept { state = value != 0 ? value : 0x9e3779b9u; }

    std::uint32_t next32() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /// Uniform in [0, 1).
    float next() noexcept
    {
        return static_cast<float>(next32() >> 8) * (1.0f / 16777216.0f);
    }

    /// Uniform in [-1, 1).
    float bipolar() noexcept { return next() * 2.0f - 1.0f; }

    /// True with probability `chance`. Reads better than comparing next()
    /// against a threshold at the call site, and is the same thing.
    bool chance(float probability) noexcept { return next() < probability; }

private:
    std::uint32_t state = 0x9e3779b9u;
};

}  // namespace valis::dsp

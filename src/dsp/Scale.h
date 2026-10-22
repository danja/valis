// src/dsp/Scale.h
//
// Musical scales as sets of pitch classes, and the two things done with them:
// snapping a pitch to one, and drawing a random pitch from one.
//
// Shared by val:Quantise, which snaps a control signal, and val:Granulator,
// which draws a pitch for each grain. Ported from the Grist plugin in
// downspout, whose tests exercise the same rules.

#pragma once

#include "dsp/Random.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace valis::dsp {

inline constexpr int kScaleCount = 7;

/// The order is the order of the scale port's scale points.
enum class ScaleKind { Free = 0, Semitones, Octaves, FifthsAndOctaves, Major, Minor, Pentatonic };

enum class Direction { Up = 0, Down = 1, Both = 2 };

namespace detail {

constexpr std::uint16_t pitchClasses(std::initializer_list<int> classes)
{
    std::uint16_t mask = 0;
    for (const int pc : classes)
        mask = static_cast<std::uint16_t>(mask | (1u << pc));
    return mask;
}

/// Bit n set means n semitones above the scale's root is allowed.
inline constexpr std::array<std::uint16_t, kScaleCount> kMasks = {
    0xfff,                                // Free, which is not quantised
    0xfff,                                // Semitones
    pitchClasses({0}),                    // Octaves
    pitchClasses({0, 7}),                 // Fifths and octaves
    pitchClasses({0, 2, 4, 5, 7, 9, 11}), // Major
    pitchClasses({0, 2, 3, 5, 7, 8, 10}), // Minor
    pitchClasses({0, 2, 4, 7, 9}),        // Pentatonic
};

}  // namespace detail

inline int clampScale(int scale) noexcept
{
    return scale < 0 ? 0 : (scale >= kScaleCount ? kScaleCount - 1 : scale);
}

/// Whether `semitones` above the root is a note of the scale.
inline bool scaleAllows(int scale, int semitones) noexcept
{
    const int pc = ((semitones % 12) + 12) % 12;
    return (detail::kMasks[static_cast<std::size_t>(clampScale(scale))] >> pc & 1u) != 0;
}

/// The note of the scale nearest `semitones`, counting from `root` (a pitch
/// class). Halfway between two, the lower wins. Free passes the value through
/// untouched, which is the only case that can return a fraction.
inline float nearestInScale(int scale, float semitones, int root = 0) noexcept
{
    if (clampScale(scale) == 0)
        return semitones;

    const int centre = static_cast<int>(std::lround(semitones));
    float best = static_cast<float>(centre);
    float bestDistance = 1.0e9f;

    // Twelve either side reaches an allowed note in every scale here, the
    // sparsest being octaves.
    for (int candidate = centre - 12; candidate <= centre + 12; ++candidate)
    {
        if (! scaleAllows(scale, candidate - root))
            continue;

        const float distance = std::abs(static_cast<float>(candidate) - semitones);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = static_cast<float>(candidate);
        }
    }
    return best;
}

/// One pitch offset around `centre` semitones: within +-`range`, in `direction`,
/// and landing on the scale (counted from the pitch itself). Free draws any
/// value in range. 0 means "stay on the centre", which is always an option, so
/// a narrow scale does not force every grain off the pitch.
inline float drawOffset(Random& random, int centre, float range, Direction direction,
                        ScaleKind scale) noexcept
{
    if (range <= 0.0f)
        return 0.0f;

    const float low  = direction == Direction::Up   ? 0.0f : -range;
    const float high = direction == Direction::Down ? 0.0f :  range;

    if (scale == ScaleKind::Free)
        return low + (high - low) * random.next();

    std::array<int, 64> candidates{};
    int count = 0;
    candidates[static_cast<std::size_t>(count++)] = 0;

    for (int offset = static_cast<int>(std::ceil(low)); offset <= static_cast<int>(std::floor(high)); ++offset)
    {
        if (offset != 0 && scaleAllows(static_cast<int>(scale), centre + offset)
            && count < static_cast<int>(candidates.size()))
            candidates[static_cast<std::size_t>(count++)] = offset;
    }

    const int pick = std::min(count - 1, static_cast<int>(random.next() * static_cast<float>(count)));
    return static_cast<float>(candidates[static_cast<std::size_t>(pick)]);
}

}  // namespace valis::dsp

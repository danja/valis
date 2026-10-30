// include/valis/SpectralDistance.h
//
// How far apart two sounds are, in dB, compared at three time resolutions on a mel scale
// (https://en.wikipedia.org/wiki/Mel_scale). Zero for identical signals. It is the check earshot
// uses to ask whether a fitted model re-renders a recording, and here it answers "did my change to
// this circuit change what it sounds like?" and "which of two renders is nearer a target?".
// Only cells within 80 dB of the loudest cell of `a` count, so silence does not flatter a match.

#pragma once

#include <vector>

namespace valis {

struct SpectralDistance
{
    double meanDb = 0.0;                ///< mean of the three resolutions
    std::vector<double> perResolution;  ///< 256, 1024 and 4096 sample frames
};

SpectralDistance spectralDistance(const std::vector<float>& a, const std::vector<float>& b, double sampleRate);

}  // namespace valis

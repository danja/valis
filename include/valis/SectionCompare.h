// include/valis/SectionCompare.h
//
// Which part of a track changed between two renders. A single distance between two files says
// that they differ; this cuts the first file at its section boundaries (see SectionMap.h) and
// measures the same stretch of each file, so a caller can say "the second chorus got darker and
// narrower" instead of "the mix changed".
//
// Both files are cut at the same times, so a render that is longer or shorter than the first, or
// whose parts have moved, compares unlike music. Sections that begin after the second file ends
// are left out, and the last one that overlaps is cut short; `lengthDifferenceSeconds` in the
// compare report says how far apart the lengths are.
//
// Pure arithmetic, message thread only: it allocates.

#pragma once

#include <vector>

namespace valis {

struct SectionDifference
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double spectralDistanceDb = 0.0;  ///< see SpectralDistance.h: 0 is the same sound
    double levelDifferenceDb = 0.0;   ///< RMS of the second file's stretch minus the first's, dB
    bool   hasWidth = false;          ///< both are stereo and both stretches had a measurable width
    double widthDifferenceDb = 0.0;   ///< second minus first, see StereoImage.h
};

struct SectionComparison
{
    std::vector<SectionDifference> sections;   ///< empty when the first file is under 20 s
    int mostChanged = -1;                      ///< index of the largest spectral distance, or -1
};

/// `rightA` and `rightB` may be empty (mono). The sample rates must be the same.
SectionComparison compareSections(const std::vector<float>& leftA, const std::vector<float>& rightA,
                                  const std::vector<float>& leftB, const std::vector<float>& rightB,
                                  double sampleRate);

}  // namespace valis

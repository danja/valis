// include/valis/SectionMap.h
//
// Where a track changes. A song is a run of sections (verse, chorus, break) that differ in
// what is playing; this finds the seconds at which the sound changes most and cuts the
// track there, then reports each section's level, its commonest pitch class and, for a
// stereo signal, its width and correlation. That lets a caller say which section of a mix
// changed between two renders.
//
// The method follows Songstress (MIT, Lowtide Signal), `listen.py`. Each second becomes a
// vector of 64 mel-band levels and 12 chroma values (the energy of each pitch class, C to
// B, whatever the octave). Each dimension is standardised over the track. Novelty at
// second t is one minus the cosine similarity between the average vector of the five
// seconds before t and of the five after; see Foote, "Automatic audio segmentation using
// a measure of audio novelty", ICME 2000, for the idea. A boundary is a local peak of
// novelty that is above the mean plus half a standard deviation and at least ten seconds
// from the last boundary.
//
// Pure arithmetic, message thread only: it allocates.

#pragma once

#include "valis/StereoImage.h"

#include <array>
#include <string>
#include <vector>

namespace valis {

/// The key that best fits a pitch-class profile, by the Krumhansl-Schmuckler method: the
/// correlation of the profile with each of 24 key templates (Krumhansl and Kessler, 1982,
/// "Tracing the dynamic changes in perceived tonal organization in a spatial representation of
/// musical keys", Psychological Review 89), taking the best. The correlation, -1 to 1, is the
/// confidence: near 0.9 is a claim, near 0.6 a guess. Relative keys (C major and A minor) share
/// notes and are easily confused.
struct KeyEstimate
{
    int    root = 0;            ///< pitch class of the tonic, 0 is C
    bool   minor = false;
    double correlation = 0.0;
};

/// `chroma` is the energy of each pitch class, C first. All zero (or all equal) gives correlation 0.
KeyEstimate estimateKey(const std::array<double, 12>& chroma);

/// "A minor", "F# major", or "none" when there was no pitch information to go on.
std::string keyName(const KeyEstimate& key);

struct Section
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double energyDb = -120.0;   ///< mean of the per-second RMS levels, dBFS
    int    dominantPitchClass = 0; ///< 0 is C, 1 is C sharp, and so on to 11 for B
    KeyEstimate key;            ///< from the section's mean pitch-class profile
    bool   hasStereo = false;
    double widthDb = 0.0;       ///< mean width of the valid seconds; see StereoImage.h
    double correlation = 1.0;
};

struct SectionMap
{
    std::vector<Section> sections;   ///< empty when the signal is shorter than 20 s
    std::vector<double> novelty;     ///< one value per second, for a caller that wants the curve
    KeyEstimate key;                 ///< of the whole track
};

/// `mono` at `sampleRate`. `stereo` is optional and adds width and correlation to each section.
SectionMap findSections(const std::vector<float>& mono, double sampleRate, const StereoImage* stereo = nullptr);

/// "C", "C#", "D" and so on for a pitch class 0 to 11.
const char* pitchClassName(int pitchClass);

}  // namespace valis

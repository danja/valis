// include/valis/StereoImage.h
//
// How wide a stereo signal is, second by second. One correlation for a whole file hides a
// chorus that opens out where the rest of the song sits in the centre; this keeps the
// figure for each second so that section can be found.
//
// Width is the level of the side signal (L minus R, halved) against the mid signal
// (L plus R, halved), in dB: well below 0 is near mono, near 0 is wide, above 0 means the
// side is louder than the middle. It is also measured in three bands (below 250 Hz, 250 Hz
// to 4 kHz, above 4 kHz), since a mix is usually kept narrow in the bass and open above.
// The idea and the choice of bands follow Songstress (MIT, Lowtide Signal), `listen.py`.
//
// Pure arithmetic, message thread only: it allocates.

#pragma once

#include <string>
#include <vector>

namespace valis {

struct StereoSecond
{
    bool   valid = false;       ///< false when the second is too quiet to say (below -80 dBFS mid)
    double widthDb = 0.0;       ///< side against mid over the whole spectrum
    double correlation = 1.0;   ///< Pearson correlation of left and right; 1 when either is constant
    double lowDb = 0.0;         ///< width below 250 Hz
    double midDb = 0.0;         ///< width from 250 Hz to 4 kHz
    double highDb = 0.0;        ///< width above 4 kHz
};

/// A stretch of seconds whose image departs from the rest of the track.
struct StereoAnomaly
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    std::string reason;
};

struct StereoImage
{
    std::vector<StereoSecond> seconds;  ///< one per whole second of the signal

    double medianWidthDb = 0.0;         ///< over the valid seconds
    double medianCorrelation = 1.0;
    double lowDb = 0.0, midDb = 0.0, highDb = 0.0;  ///< mean width of each band over the valid seconds
    long   validSeconds = 0;

    /// Ten-second windows whose width is 4 dB or more from the median, or whose correlation
    /// is 0.35 or more below it. Advisory: the numbers are the measurement.
    std::vector<StereoAnomaly> anomalies;
};

/// Two channels of equal length at `sampleRate`. Fewer than one second gives no seconds.
StereoImage analyseStereoImage(const std::vector<float>& left, const std::vector<float>& right,
                               double sampleRate);

}  // namespace valis

// include/valis/EnvelopeTimes.h
//
// How a single sound starts and stops, from its RMS envelope: the time from 10% to 90% of the
// first rise, and the time from the peak until the level is 30 dB down and stays there. A key
// press, a pluck and a pad give very different answers, so these are what a caller compares
// when it cannot listen. The definitions are earshot's (`features.envelope_times`).

#pragma once

#include <vector>

namespace valis {

struct EnvelopeTimes
{
    double attackAtMs = 0.0;   ///< where the sound starts: first millisecond within 20 dB of the loudest
    double peakAtMs = 0.0;
    double attackMs = -1.0;    ///< 10% to 90% rise of the first event; -1 if there is none
    double decayMs = -1.0;     ///< peak to 30 dB down; -1 when it never gets there (a sustained sound)
    double noiseFloorDbfs = -120.0; ///< 10th percentile of 20 ms RMS
    double dynamicRangeDb = 0.0;    ///< loudest 20 ms RMS above that floor
};

EnvelopeTimes measureEnvelope(const std::vector<float>& mono, double sampleRate);

}  // namespace valis

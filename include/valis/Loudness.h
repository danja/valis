// include/valis/Loudness.h
//
// Programme loudness as a broadcast meter reports it: integrated loudness in LUFS, loudness range
// in LU and true peak in dBTP, after ITU-R BS.1770-4 (https://www.itu.int/rec/R-REC-BS.1770) and
// EBU R 128 (https://tech.ebu.ch/docs/r/r128.pdf). Ported from earshot's `features.py`, which uses
// the same measures to judge a soundtrack. Pure arithmetic over samples; message thread only.

#pragma once

#include <vector>

namespace valis {

struct LoudnessReport
{
    double integratedLufs = -120.0; ///< gated mean loudness of the whole signal
    double rangeLu = 0.0;           ///< spread of short-term loudness, 10th to 95th percentile
    double shortTermMaxLufs = -120.0;
    double momentaryMaxLufs = -120.0;
    double truePeakDbtp = -120.0;   ///< peak after 4x oversampling, so it sees inter-sample peaks
};

/// `right` may be empty for a mono signal, which is measured as if both channels carried it.
LoudnessReport measureLoudness(const std::vector<float>& left, const std::vector<float>& right,
                               double sampleRate);

}  // namespace valis

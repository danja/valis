// include/valis/ClapFrontend.h
//
// The audio front end of CLAP (https://github.com/LAION-AI/CLAP), as Hugging Face's ClapFeatureExtractor does it for
// laion/larger_clap_general: 48 kHz mono, exactly ten seconds, a Hann-windowed 1024-point power spectrum every 480
// samples, 64 Slaney-scale mel bands from 50 Hz to 14 kHz, in dB. Pure arithmetic, so it is tested without the model.

#pragma once

#include <vector>

namespace valis {

constexpr int kClapRate = 48000;
constexpr int kClapSamples = 480000;   ///< ten seconds
constexpr int kClapFrames = 1001;
constexpr int kClapMels = 64;

/// Ten seconds of audio for the model: shorter sounds are repeated as many whole times as fit and then padded with
/// silence ("repeatpad", what CLAP trained with); longer ones are cut. `zeroPad` pads with silence only, which
/// suits judging a single hit.
std::vector<float> clapPrepare(const std::vector<float>& mono, bool zeroPad = false);

/// kClapFrames x kClapMels log-mel values, frame-major, from exactly kClapSamples samples.
std::vector<float> clapLogMel(const std::vector<float>& tenSeconds);

}  // namespace valis

// include/valis/AstFrontend.h
//
// The audio front end of the Audio Spectrogram Transformer (https://arxiv.org/abs/2104.01778), as Hugging Face's
// ASTFeatureExtractor does it: 16 kHz mono, Kaldi-style 128-band log-mel filterbank (25 ms frames every 10 ms,
// https://kaldi-asr.org/doc/feat.html), padded or cut to 1024 frames and normalised. Pure arithmetic.

#pragma once

#include <vector>

namespace valis {

constexpr int kAstRate = 16000;
constexpr int kAstFrames = 1024;
constexpr int kAstMels = 128;

/// kAstFrames x kAstMels values, frame-major, from mono audio at kAstRate.
std::vector<float> astFilterbank(const std::vector<float>& mono16k);

/// Linear-interpolated resampling to another rate; enough for a measurement front end.
std::vector<float> resampleLinear(const std::vector<float>& x, double fromRate, double toRate);

}  // namespace valis

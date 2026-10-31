// include/valis/ModalFit.h
//
// Turns a recording of a struck object (a bell, a cowbell, a conga) into the modes of a val:FreeModalBank: a
// list of damped sines, each with a frequency, a ring time and a starting level. Ideas from earshot's fit/modal.py
// (https://github.com/Leonxlnx/earshot); this port finds the modes by spectral peaks and measures each one's decay
// from a short-time Fourier transform of that frequency, not by ESPRIT, which needs an eigen-solver.
//
// Message thread only. Not real-time.

#pragma once

#include <string>
#include <vector>

namespace valis {

struct FittedMode
{
    double hertz   = 0.0;
    double seconds = 0.0;   ///< time to fall 60 dB
    double gain    = 0.0;   ///< starting level, as a val:FreeModalBank mode played by a unit impulse
};

/// At most `maxModes` modes, sorted by frequency. The sound is taken from its first sample above a tenth of its
/// peak, for up to 1.5 seconds. Modes closer than about 10 Hz are merged into the stronger, and a mode that rings
/// for under about 0.1 second is measured coarsely, since the analysis window is 85 ms long. Returns an empty
/// list for silence or a recording under 4096 samples.
std::vector<FittedMode> fitModes(const std::vector<float>& mono, double sampleRate, int maxModes);

/// The same modes written as a val:modes value: "hertz:seconds:gain" apiece, separated by spaces.
std::string modesToString(const std::vector<FittedMode>& modes);

/// What the modes sound like when a unit impulse strikes them: the sum of gain * r^n * sin((n + 1) w), as
/// val:FreeModalBank plays it, for `samples` samples.
std::vector<float> renderModes(const std::vector<FittedMode>& modes, double sampleRate, std::size_t samples);

}  // namespace valis

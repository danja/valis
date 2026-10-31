// include/valis/TempoEstimate.h
//
// The speed of a pulse, in beats per minute, with the other speeds it might be. Tempo from a
// recording is ambiguous: a pulse at 130 BPM also repeats at 65 and at 260, and which one a
// listener calls the beat is a matter of feel. So this reports the strongest candidate and
// the next few, each with how strongly the signal repeats at that period.
//
// The method is the usual one. Spectral flux (the rise in energy in each frequency band from
// one short frame to the next) marks where sounds begin. The autocorrelation of that curve
// shows at which delays it repeats; the peaks between 40 and 200 BPM are the candidates.
// See Scheirer, "Tempo and beat analysis of acoustic musical signals", JASA 103, 1998, and
// Songstress (MIT, Lowtide Signal), `listen.py`, which this follows.
//
// Pure arithmetic, message thread only: it allocates.

#pragma once

#include <vector>

namespace valis {

struct TempoCandidate
{
    double bpm = 0.0;
    double strength = 0.0;   ///< autocorrelation at this period over that at zero delay, 0 to 1: how regular the pulse is
};

struct TempoEstimate
{
    bool found = false;      ///< false for silence, a signal under 8 s, or no repeating pulse
    std::vector<TempoCandidate> candidates;   ///< most likely first, which weighs strength by a preference for tempos near 120; at most 4, no two within 3% of each other

    double bpm() const { return candidates.empty() ? 0.0 : candidates.front().bpm; }
};

/// `mono` at `sampleRate`.
TempoEstimate estimateTempo(const std::vector<float>& mono, double sampleRate);

}  // namespace valis

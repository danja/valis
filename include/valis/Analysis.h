// include/valis/Analysis.h
//
// Measuring a rendered signal, for a caller that cannot listen. Peak and RMS say whether
// there is sound; this says what kind: the pitch it sits on, how loud each octave is, how
// bright it is, when notes start and how the level moves, and whether anything is wrong
// with it (silence, clipping, a DC offset, values that are not numbers).
//
// Pure arithmetic over a vector of samples: no files, no engine, no thread rules beyond
// the message thread, since it allocates. The pitch estimator is YIN (de Cheveigne and
// Kawahara, "YIN, a fundamental frequency estimator for speech and music", JASA 2002).

#pragma once

#include <string>
#include <vector>

namespace valis {

struct PitchEstimate
{
    bool   found = false;
    double hz = 0.0;
    int    midiNote = 0;        ///< the nearest equal-tempered note
    double cents = 0.0;         ///< how far hz is from that note, -50 to 50
    double voicedFraction = 0.0;///< share of the analysed frames that had a clear pitch
    double stability = 0.0;     ///< share of the voiced frames within 50 cents of hz
};

struct SignalReport
{
    double seconds = 0.0;
    double sampleRate = 0.0;
    int    channels = 1;

    double peak = 0.0;          ///< largest absolute sample, either channel
    double rms = 0.0;           ///< over all samples of all channels
    double dc = 0.0;            ///< mean of the mono mix
    long   clipped = 0;         ///< samples at or past full scale
    long   nonFinite = 0;       ///< NaN or infinite samples
    double silentFraction = 0.0;///< share of 50 ms windows below -80 dBFS
    double firstSoundAt = -1.0; ///< seconds to the first window above -60 dBFS, or -1

    PitchEstimate pitch;

    /// Level of each octave band centred on 63 Hz up to 16 kHz, in dBFS RMS. -120 means
    /// nothing there.
    std::vector<double> bandsDb;
    double centroidHz = 0.0;    ///< the spectrum's centre of mass: higher is brighter
    double rolloffHz = 0.0;     ///< below which lies 85% of the power

    std::vector<double> onsets;     ///< seconds at which a new sound begins (at most 64)
    std::vector<double> envelopeDb; ///< RMS in dBFS for each quarter second (at most 80)

    double stereoCorrelation = 1.0; ///< 1 identical, 0 unrelated, -1 opposed; 2-channel only
    double balanceDb = 0.0;         ///< right level minus left level; 2-channel only

    /// Plain-language findings, worth reading before the numbers.
    std::vector<std::string> notes;
};

/// Analyses one or two channels (`right` may be empty). Sample rate must be positive.
SignalReport analyseSignal(const std::vector<float>& left, const std::vector<float>& right,
                           double sampleRate);

/// The report as one JSON object.
std::string reportToJson(const SignalReport& report);

/// "A4", "F#2" and so on, for a MIDI note number 0 to 127.
std::string noteName(int midiNote);

}  // namespace valis

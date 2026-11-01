// include/valis/SongMix.h
//
// The reference mixdown of a song (see docs/songs.md): each stem is levelled to its val:level by its playing RMS,
// high-passed, panned and summed; then the sections' levels, a fade, a synthetic room, a soft clip, the peak set to
// -1 dBFS and, if the song asks, the gain that reaches its loudness without passing -1 dBTP. Vocals and mastering
// are done in a DAW; this is the mix to judge the arrangement by. Pure arithmetic over samples, with the file
// handling kept apart so the arithmetic can be tested without a file.

#pragma once

#include "valis/AudioFile.h"
#include "valis/Loudness.h"
#include "valis/Song.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace valis {

/// A stem's samples with the mix settings it is mixed by.
struct MixStem
{
    std::string name;
    StereoAudio audio;
    double levelDb = -20.0;     ///< target playing RMS
    double pan = 0.0;
    double highpassHz = 0.0;
    std::string follows;        ///< another stem's name: take its gain, and `levelDb` is an offset from it
};

struct MixOptions
{
    bool arc = false;                   ///< apply each section's val:level
    std::optional<double> loudness;     ///< LUFS to aim at; the song's own if none
};

struct MixStemReport
{
    std::string name;
    double measuredDb = 0.0;            ///< playing RMS before levelling, after the high-pass
    double gainDb = 0.0;                ///< the gain that levelled it
};

struct MixReport
{
    StereoAudio mix;
    LoudnessReport loudness;            ///< of the finished mix
    std::optional<double> targetLufs;
    double loudnessGainDb = 0.0;        ///< applied toward the target
    bool limitedByPeak = false;         ///< the -1 dBTP ceiling, not the target, set that gain
    std::vector<std::pair<std::string, double>> sections;   ///< playing RMS in dB of each section of the mix
    std::vector<MixStemReport> stems;
};

/// Mixes stems that are all at one sample rate. Returns false with the reason if there are none, they disagree on
/// the rate, or the song has no length.
bool mixStems(const Song& song, std::vector<MixStem>& stems, const MixOptions& options, MixReport& report,
              std::string& error);

/// What `mixSongFiles` found and wrote.
struct MixFilesResult
{
    bool ok = false;
    std::string error;
    std::string outputPath;
    std::vector<std::string> missing;   ///< stems whose wav was not in the directory; the mix goes ahead without them
    MixReport report;
};

/// Reads `<wavDirectory>/<stem>.wav` for each stem of the song, mixes them, and writes a 24-bit wav to `outputPath`.
MixFilesResult mixSongFiles(const Song& song, const std::string& wavDirectory, const std::string& outputPath,
                            const MixOptions& options);

// -- the parts, exposed so they can be tested alone
namespace songmix {

/// RMS in dB over the tenth-second windows of the signal whose RMS is above 0.001, so a sparse part is judged by when
/// it plays. -120 if no window is that loud.
double playingRmsDb(const std::vector<float>& mono, double sampleRate);

/// Linear convolution of `signal` with `impulse` by FFT, as long as the signal. Equal to the direct sum to within
/// float precision.
std::vector<float> fftConvolve(const std::vector<float>& signal, const std::vector<float>& impulse);

/// Second-order Butterworth high-pass, run once forward from zero state.
void highpass(std::vector<float>& x, double cornerHz, double sampleRate);

/// Gain curve (linear) that steps between the sections' levels and is smoothed over about two seconds.
std::vector<float> arcCurve(const Song& song, double sampleRate, std::size_t length);

} // namespace songmix

} // namespace valis

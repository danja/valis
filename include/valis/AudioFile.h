// include/valis/AudioFile.h
//
// Stereo audio read from and written to a wav file, for the offline operations (a song's render and mix). Message
// thread or a worker: it reads and writes files, and holds no state.

#pragma once

#include <string>
#include <vector>

namespace valis {

struct StereoAudio
{
    std::vector<float> left, right;
    double sampleRate = 0.0;

    std::size_t frames() const noexcept { return left.size(); }
};

/// Reads a sound file (wav, aiff, flac or ogg) as stereo: a mono file is copied to both sides, and a file of more
/// than two channels keeps its first two. A relative path is read against the working directory.
bool readAudioFile(const std::string& path, StereoAudio& audio, std::string& error);

/// Writes a 24-bit stereo wav, replacing any file there, and makes the directory if it is missing.
bool writeWavFile(const std::string& path, const StereoAudio& audio, std::string& error);

} // namespace valis

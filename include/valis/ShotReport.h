// include/valis/ShotReport.h
//
// The three valis-shot measurements as functions that return JSON, so the command line tool, the operation surface
// and the MCP server share one implementation. Ideas from earshot (https://github.com/Leonxlnx/earshot); see docs/shot.md.
//
// A path is read relative to the working directory unless absolute. Only the first ten minutes of a file are read, to
// bound the memory. Message thread only: listen runs neural models for hundreds of milliseconds.

#pragma once

#include <string>
#include <vector>

namespace valis {

struct ShotResult
{
    bool ok = false;
    std::string json;    ///< one JSON object when ok
    std::string error;   ///< why not, when not ok
};

/// Signal analysis (as analyseSignal), BS.1770 loudness and attack and decay times, for one file.
ShotResult shotMeasure(const std::string& path);

/// How far apart two renders are: multi-resolution spectral distance in dB and the loudness and length differences.
/// The sample rates must match.
ShotResult shotCompare(const std::string& pathA, const std::string& pathB);

/// What the sound is (AudioSet labels from the loudest ten seconds) and, for each sentence, how well it fits (CLAP
/// cosine and a share among the sentences). Needs -DVALIS_WITH_ONNX=ON and scripts/shot/fetch-models.sh; otherwise
/// not ok, with that reason. The models load on first use and stay in memory (about 1.1 GB on disk).
ShotResult shotListen(const std::string& path, const std::vector<std::string>& queries);

/// The one-question form of listen: is this sound "a trumpet"? The sentence is scored against twenty common rivals
/// (other instruments, a synthesizer preset, noise, a sine), and the answer is a verdict (yes at a share of 0.5 or
/// more, maybe from 0.2, else no), the share, the sentence's rank among all of them and the three closest sentences.
/// Needs the same models as listen.
ShotResult shotIs(const std::string& path, const std::string& sentence);

/// Whether this build can listen, and if not why.
bool shotCanListen(std::string& reason);

}  // namespace valis

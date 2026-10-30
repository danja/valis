// include/valis/NeuralEars.h
//
// Neural models that let a caller who cannot hear ask about a sound, run locally on the CPU through ONNX Runtime
// (https://onnxruntime.ai). Built only with -DVALIS_WITH_ONNX=ON; without it load() fails with that reason.
//
//   CLAP  one embedding space for sounds and sentences (https://github.com/LAION-AI/CLAP, laion/larger_clap_general,
//         Apache-2.0). The cosine of a sound's embedding and a sentence's says how well they fit.
//   AST   labels a sound with the 527 AudioSet classes (https://arxiv.org/abs/2104.01778, BSD-3-Clause).
//
// The weights are not in the repository. scripts/shot/fetch-models.sh downloads them, pinned and checked, into a
// folder holding clap/ and ast/. Message thread only: it allocates and runs for hundreds of milliseconds.

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace valis {

struct AudioTag
{
    std::string label;
    float probability = 0.0f;
};

class NeuralEars
{
public:
    NeuralEars();
    ~NeuralEars();

    /// The default folder: $VALIS_MODELS, else ~/.local/share/valis/models.
    static std::string defaultModelsDir();

    /// Loads what is present. Returns false, with a reason naming the missing file, if neither model could load.
    bool load(const std::string& modelsDir, std::string& error);
    bool hasClap() const;
    bool hasAst() const;

    /// 512 numbers of length 1, for the first ten seconds of the sound (longer sounds: windows of ten seconds every
    /// five, averaged). Empty if CLAP is not loaded.
    std::vector<float> embedAudio(const std::vector<float>& mono, double sampleRate, bool zeroPad = false);
    std::vector<float> embedText(const std::string& sentence);

    /// The strongest AudioSet labels, most probable first.
    std::vector<AudioTag> tags(const std::vector<float>& mono, double sampleRate, int top = 8);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/// Dot product of two vectors of length 1.
float cosine(const std::vector<float>& a, const std::vector<float>& b);

}  // namespace valis

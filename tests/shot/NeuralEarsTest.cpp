// tests/shot/NeuralEarsTest.cpp
//
// valis::NeuralEars with the real CLAP and AST models. Built only with VALIS_WITH_ONNX, and skipped when the weights are
// not in $VALIS_MODELS (run scripts/shot/fetch-models.sh). A model is fuzzy, so these test only differences it must get
// right: a plucked string is nearer "a plucked string" than a burst of hiss is, and the other way round.

#include "valis/NeuralEars.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace valis;

namespace {

constexpr double kRate = 48000.0;

/// A Karplus-Strong pluck at 196 Hz: a noise burst round a delay line with a lossy average.
std::vector<float> pluck()
{
    const int delay = static_cast<int>(kRate / 196.0);
    std::vector<float> line(static_cast<std::size_t>(delay)), out(static_cast<std::size_t>(kRate * 3.0));
    unsigned state = 7;
    for (auto& v : line)
    {
        state = state * 1664525u + 1013904223u;
        v = static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const std::size_t k = i % line.size(), next = (i + 1) % line.size();
        out[i] = line[k];
        line[k] = 0.4975f * (line[k] + line[next]);
    }
    return out;
}

std::vector<float> hiss()
{
    std::vector<float> out(static_cast<std::size_t>(kRate * 3.0));
    unsigned state = 99;
    for (auto& v : out)
    {
        state = state * 1664525u + 1013904223u;
        v = 0.3f * (static_cast<float>(state >> 8) / 8388608.0f - 1.0f);
    }
    return out;
}

std::vector<float> sine(double hz)
{
    std::vector<float> out(static_cast<std::size_t>(kRate * 3.0));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = 0.3f * static_cast<float>(std::sin(6.283185307179586 * hz * static_cast<double>(i) / kRate));
    return out;
}

/// Where a label ranks among the model's top n, or -1.
int rankOf(const std::vector<AudioTag>& tags, const std::string& label)
{
    for (std::size_t i = 0; i < tags.size(); ++i)
        if (tags[i].label == label)
            return static_cast<int>(i);
    return -1;
}

}  // namespace

int main()
{
    NeuralEars ears;
    std::string error;
    if (! ears.load(NeuralEars::defaultModelsDir(), error))
    {
        std::printf("NeuralEarsTest SKIPPED (%s)\n", error.c_str());
        return 0;
    }

    if (ears.hasClap())
    {
        const auto string = ears.embedAudio(pluck(), kRate), noise = ears.embedAudio(hiss(), kRate);
        assert(string.size() == 512 && std::abs(cosine(string, string) - 1.0f) < 1.0e-4f);
        assert(ears.embedAudio(pluck(), kRate) == string);                               // deterministic

        const auto aString = ears.embedText("a plucked string"), aHiss = ears.embedText("white noise, a hiss");
        assert(aString.size() == 512 && std::abs(cosine(aString, aString) - 1.0f) < 1.0e-4f);
        std::printf("  clap: pluck vs 'plucked string' %.3f, vs 'hiss' %.3f; hiss vs 'plucked string' %.3f, vs 'hiss' %.3f\n",
                    cosine(string, aString), cosine(string, aHiss), cosine(noise, aString), cosine(noise, aHiss));
        assert(cosine(string, aString) > cosine(string, aHiss));
        assert(cosine(noise, aHiss) > cosine(noise, aString));

        // The mel axis: a low tone is nearer "a low rumble" and a high tone nearer "a high whistle", not the reverse.
        const auto low = ears.embedAudio(sine(80.0), kRate), high = ears.embedAudio(sine(5000.0), kRate);
        const auto aLow = ears.embedText("a low, deep rumble"), aHigh = ears.embedText("a high pitched whistle");
        std::printf("  clap: 80 Hz vs low %.3f high %.3f; 5 kHz vs low %.3f high %.3f\n", cosine(low, aLow), cosine(low, aHigh),
                    cosine(high, aLow), cosine(high, aHigh));
        assert(cosine(low, aLow) > cosine(low, aHigh));
        assert(cosine(high, aHigh) > cosine(high, aLow));
    }
    if (ears.hasAst())
    {
        const auto tags = ears.tags(pluck(), kRate, 5);
        assert(tags.size() == 5);
        for (std::size_t i = 0; i < tags.size(); ++i)
        {
            std::printf("  ast: %s %.2f\n", tags[i].label.c_str(), tags[i].probability);
            assert(tags[i].probability >= 0.0f && tags[i].probability <= 1.0f);
            assert(i == 0 || tags[i].probability <= tags[i - 1].probability);
        }
    }
    if (ears.hasAst())
    {
        // Known answers: AudioSet has classes for exactly these signals, so a wrong mel scale, window or normalisation
        // in the front end shows up as the model failing to name them.
        const auto tone = ears.tags(sine(440.0), kRate, 10), noise = ears.tags(hiss(), kRate, 10);
        std::printf("  ast sine: rank of 'Sine wave' %d; hiss: rank of 'White noise' %d, 'Noise' %d\n",
                    rankOf(tone, "Sine wave"), rankOf(noise, "White noise"), rankOf(noise, "Noise"));
        assert(rankOf(tone, "Sine wave") == 0 && tone[0].probability > 0.5f);
        assert(rankOf(noise, "White noise") >= 0);
    }
    std::printf("NeuralEarsTest PASSED\n");
    return 0;
}

// src/dsp/elements/NamModel.cpp
//
// val:NamModel runs a Neural Amp Modeler capture (a .nam file) as an audio
// element. Built without VALIS_WITH_NAM the class still exists, so the ontology
// and the registry agree, but loading a model fails with a message that says why.

#include "Common.h"

#include <juce_core/juce_core.h>

#ifdef VALIS_WITH_NAM
 #include "dsp/ResolveFile.h"

 #include <juce_cryptography/juce_cryptography.h>

 #include "NAM/dsp.h"
 #include "NAM/get_dsp.h"
#endif

namespace valis::elements {

class NamModel final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int maxBlockSize) override
    {
        sampleRate = rate;
        capacity   = std::max(maxBlockSize, 1);
        inGainIdx  = controlIndex(type, "input");
        outGainIdx = controlIndex(type, "output");
        bypassIdx  = controlIndex(type, "bypass");

        // The model is handed whole blocks of at most this many frames, so the
        // network sizes its buffers for it once and never again.
        inBuffer.assign(static_cast<std::size_t>(capacity), 0.0f);
        outBuffer.assign(static_cast<std::size_t>(capacity), 0.0f);
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error) override
    {
        if (key == "sha256")
        {
            // 64 hex characters, or it is not a SHA-256 and never will match. This is
            // the one thing that fails the load: it is a typo in the circuit, not a
            // file that is missing on this machine.
            const juce::String text{std::string(value)};
            if (text.length() != 64 || ! text.containsOnly("0123456789abcdefABCDEF"))
            {
                error = "val:sha256 must be 64 hexadecimal characters, not '" + std::string(value) + "'";
                return false;
            }

            // Order independent: RDF has no statement order, so the model may already be loaded.
            expectedHash = text.toLowerCase().toStdString();
            if (! modelPath.empty())
                loadModel(modelPath);
            return true;
        }

        if (key != "model")
            return true;

        modelPath = std::string(value);
        loadModel(modelPath);
        return true;
    }

    void loadModel(std::string_view value)
    {
        // Nothing here fails the load. A circuit shared to another machine
        // names a file that is not there, and a model can be corrupt or trained
        // at another rate; refusing the circuit for any of these would leave no
        // place to choose a different file. The element passes its input
        // through and says what is wrong through status().
        model.reset();
        problem.clear();

#ifdef VALIS_WITH_NAM
        const auto file = dsp::resolveFile(value);
        if (! file.existsAsFile())
        {
            problem = "Not found: " + file.getFileName().toStdString();
            return;
        }

        if (! expectedHash.empty())
        {
            juce::FileInputStream stream(file);
            const auto actual = stream.openedOk() ? juce::SHA256(stream).toHexString().toStdString()
                                                  : std::string();
            if (actual != expectedHash)
            {
                problem = file.getFileName().toStdString() + " is not the file declared: val:sha256 is "
                        + expectedHash.substr(0, 12) + "..., the file's is " + actual.substr(0, 12) + "...";
                return;
            }
        }

        try
        {
            auto loaded = nam::get_dsp(std::filesystem::path(file.getFullPathName().toStdString()));
            if (loaded == nullptr)
            {
                problem = "Unreadable model: " + file.getFileName().toStdString();
                return;
            }

            // TODO: resample around a model trained at a rate other than the
            // engine's; until then it is refused rather than run out of tune.
            const double trained = loaded->GetExpectedSampleRate();
            if (trained > 0.0 && ! loaded->SupportsArbitrarySampleRate()
                && std::abs(trained - sampleRate) > 0.5)
            {
                problem = "Trained at " + std::to_string(static_cast<int>(trained))
                        + " Hz; the engine runs at " + std::to_string(static_cast<int>(sampleRate))
                        + " Hz";
                return;
            }

            loaded->Reset(sampleRate, capacity);
            model = std::move(loaded);
        }
        catch (const std::exception&)
        {
            // The parser's own message runs to several lines and would not fit
            // the file box; the name is enough to find the file.
            problem = "Not a valid NAM model: " + file.getFileName().toStdString();
        }
        return;
#else
        (void) value;
        problem = "This build has no Neural Amp Modeler support";
        return;
#endif
    }

    std::string status() const override { return problem; }

    void reset() override
    {
        lastIn = lastOut = 1.0f;
#ifdef VALIS_WITH_NAM
        if (model != nullptr)
            model->Reset(sampleRate, capacity);
#endif
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];
        const int    n   = args.numSamples;

        const bool  bypassed = controlAt(args, bypassIdx, 0.0f) > 0.5f;
        const float inGain   = dbToGain(controlAt(args, inGainIdx, 0.0f));
        const float outGain  = dbToGain(controlAt(args, outGainIdx, 0.0f));

#ifndef VALIS_WITH_NAM
        (void) bypassed; (void) inGain; (void) outGain;
        std::copy(in, in + n, out);
#else
        if (model == nullptr || bypassed)
        {
            std::copy(in, in + n, out);
            return;
        }

        // Gain moves in a straight line across the block, so turning a dial
        // does not step the signal into a nonlinear network.
        for (int start = 0; start < n; start += capacity)
        {
            const int frames = std::min(capacity, n - start);
            const float t0 = static_cast<float>(start) / static_cast<float>(n);
            const float t1 = static_cast<float>(start + frames) / static_cast<float>(n);

            ramp(in + start, inBuffer.data(), frames,
                 lastIn + (inGain - lastIn) * t0, lastIn + (inGain - lastIn) * t1);

            float* inPtr  = inBuffer.data();
            float* outPtr = outBuffer.data();
            model->process(&inPtr, &outPtr, frames);

            ramp(outBuffer.data(), out + start, frames,
                 lastOut + (outGain - lastOut) * t0, lastOut + (outGain - lastOut) * t1);
        }

        lastIn  = inGain;
        lastOut = outGain;
#endif
    }

private:
    static void ramp(const float* from, float* to, int frames, float g0, float g1) noexcept
    {
        const float step = frames > 0 ? (g1 - g0) / static_cast<float>(frames) : 0.0f;
        float g = g0;
        for (int i = 0; i < frames; ++i, g += step)
            to[i] = from[i] * g;
    }

    double sampleRate = 48000.0;
    int capacity = 1;
    std::string problem;
    std::string modelPath;      ///< what val:model named, kept so a late val:sha256 can recheck it
    std::string expectedHash;   ///< the declared val:sha256, lowercase, or empty
    int inGainIdx = -1, outGainIdx = -1, bypassIdx = -1;
    float lastIn = 1.0f, lastOut = 1.0f;
    std::vector<float> inBuffer, outBuffer;

#ifdef VALIS_WITH_NAM
    std::unique_ptr<nam::DSP> model;
#endif
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerNam(ElementRegistry& registry)
{
    registry.add("NamModel", &make<elements::NamModel>);
}
}  // namespace valis

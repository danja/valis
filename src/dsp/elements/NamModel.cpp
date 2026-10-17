// src/dsp/elements/NamModel.cpp
//
// val:NamModel runs a Neural Amp Modeler capture (a .nam file) as an audio
// element. Built without VALIS_WITH_NAM the class still exists, so the ontology
// and the registry agree, but loading a model fails with a message that says why.

#include "Common.h"

#ifdef VALIS_WITH_NAM
 #include "dsp/SampleFile.h"

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
        if (key != "model")
            return true;

#ifdef VALIS_WITH_NAM
        const auto file = dsp::SampleFile::resolve(value);
        if (! file.existsAsFile())
        {
            error = "no such model file: " + file.getFullPathName().toStdString();
            return false;
        }

        // TODO: honour a declared val:sha256, as val:SampleLoad does.
        std::unique_ptr<nam::DSP> loaded;
        try
        {
            loaded = nam::get_dsp(std::filesystem::path(file.getFullPathName().toStdString()));
            if (loaded == nullptr)
            {
                error = "unreadable model: " + file.getFullPathName().toStdString();
                return false;
            }

            // TODO: resample around a model trained at a rate other than the
            // engine's; until then it is refused rather than run out of tune.
            const double trained = loaded->GetExpectedSampleRate();
            if (trained > 0.0 && ! loaded->SupportsArbitrarySampleRate()
                && std::abs(trained - sampleRate) > 0.5)
            {
                error = "model " + file.getFileName().toStdString() + " was trained at "
                      + std::to_string(static_cast<int>(trained)) + " Hz and the engine runs at "
                      + std::to_string(static_cast<int>(sampleRate)) + " Hz";
                return false;
            }

            loaded->Reset(sampleRate, capacity);
        }
        catch (const std::exception& e)
        {
            error = "model " + file.getFileName().toStdString() + ": " + e.what();
            return false;
        }

        model = std::move(loaded);
        return true;
#else
        (void) value;
        error = "this build has no Neural Amp Modeler support (configure with -DVALIS_WITH_NAM=ON)";
        return false;
#endif
    }

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

#ifdef VALIS_WITH_NAM
        if (model == nullptr || bypassed)
#else
        if (true)
#endif
        {
            std::copy(in, in + n, out);
            return;
        }

#ifdef VALIS_WITH_NAM
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

// src/dsp/elements/Utility.cpp

#include "Common.h"

namespace valis::elements {

class Gain final : public MonoElement
{
protected:
    void cacheIndices(const ElementType& type) override { gainIndex = controlIndex(type, "gain"); }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const float g = dbToGain(controlAt(args, gainIndex, 0.0f));
        for (int i = 0; i < n; ++i)
            out[i] = in[i] * g;
    }

private:
    int gainIndex = -1;
};

/// Voltage-controlled amplifier. Multiplies audio sample-by-sample by a 0–1
/// control signal, giving proper linear amplitude modulation from an envelope.
class VCA final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        cvIndex = controlIndex(type, "cv");
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float cv  = std::clamp(controlAt(args, cvIndex, 1.0f), 0.0f, 1.0f);
        const float* in = args.audioIn[0];
        float*      out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
            out[i] = in[i] * cv;
    }

private:
    int cvIndex = -1;
};

/// Sums whatever arrives at its input. The engine has already added the arcs
/// together into that buffer, which is why fan-in onto anything else is a
/// compile error: only this element is documented to sum.
class Mixer final : public MonoElement
{
protected:
    void processMono(const float* in, float* out, int n, const ProcessArgs&) noexcept override
    {
        std::copy(in, in + n, out);
    }
};

class DryWet final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        dryIn    = audioInIndex(type, "dry");
        wetIn    = audioInIndex(type, "wet");
        mixIndex = controlIndex(type, "mix");
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float mix = std::clamp(controlAt(args, mixIndex, 1.0f), 0.0f, 1.0f);

        const float* dry = dryIn >= 0 && dryIn < args.numAudioIn ? args.audioIn[dryIn] : nullptr;
        const float* wet = wetIn >= 0 && wetIn < args.numAudioIn ? args.audioIn[wetIn] : nullptr;
        float* out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            const float d = dry != nullptr ? dry[i] : 0.0f;
            const float w = wet != nullptr ? wet[i] : 0.0f;
            out[i] = d + mix * (w - d);
        }
    }

private:
    int dryIn = -1, wetIn = -1, mixIndex = -1;
};

/// Remaps a 0–1 control signal to [min, max]. Bridges Envelope (0–1 output)
/// to targets that expect physical units such as Hz or ms.
class Scale final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        inIndex  = controlIndex(type, "in");
        minIndex = controlIndex(type, "min");
        maxIndex = controlIndex(type, "max");
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 1)
            return;
        const float in = controlAt(args, inIndex,  0.0f);
        const float lo = controlAt(args, minIndex, 0.0f);
        const float hi = controlAt(args, maxIndex, 1.0f);
        args.controlOut[0] = lo + in * (hi - lo);
    }

private:
    int inIndex = -1, minIndex = -1, maxIndex = -1;
};

/// Single-tap delay line with feedback. Preallocated in prepare(); process()
/// never allocates. Maximum delay: 5 seconds at the current sample rate.
class Delay final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        timeIndex     = controlIndex(type, "time");
        feedbackIndex = controlIndex(type, "feedback");
        buffer.assign(static_cast<std::size_t>(rate * 5.0) + 1, 0.0f);
        writePos = 0;
    }

    void reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float timeMs   = std::clamp(controlAt(args, timeIndex,     250.0f), 0.0f, 5000.0f);
        const float feedback = std::clamp(controlAt(args, feedbackIndex,   0.0f), 0.0f, 0.99f);
        const int   delayN   = std::max(1, static_cast<int>(timeMs * 0.001 * sampleRate));
        const int   bufSize  = static_cast<int>(buffer.size());

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            int readPos = writePos - delayN;
            if (readPos < 0)
                readPos += bufSize;

            const float delayed = buffer[static_cast<std::size_t>(readPos)];
            buffer[static_cast<std::size_t>(writePos)] = in[i] + delayed * feedback;
            out[i] = delayed;

            if (++writePos >= bufSize)
                writePos = 0;
        }
    }

private:
    std::vector<float> buffer;
    int writePos = 0;
    double sampleRate = 44100.0;
    int timeIndex = -1, feedbackIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerUtility(ElementRegistry& registry)
{
    registry.add("Gain",   &make<elements::Gain>);
    registry.add("VCA",    &make<elements::VCA>);
    registry.add("Scale",  &make<elements::Scale>);
    registry.add("Delay",  &make<elements::Delay>);
    registry.add("Mixer",  &make<elements::Mixer>);
    registry.add("DryWet", &make<elements::DryWet>);
}

// The one place that knows the whole set. makeDefaultRegistry's keys and the
// ontology's val:implementation values must match exactly; a test asserts it.
void registerSources(ElementRegistry&);
void registerFilters(ElementRegistry&);
void registerTransfers(ElementRegistry&);
void registerDynamics(ElementRegistry&);

ElementRegistry makeDefaultRegistry()
{
    ElementRegistry registry;
    registerSources(registry);
    registerFilters(registry);
    registerTransfers(registry);
    registerDynamics(registry);
    registerUtility(registry);
    return registry;
}
}  // namespace valis

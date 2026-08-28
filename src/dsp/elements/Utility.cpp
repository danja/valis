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

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerUtility(ElementRegistry& registry)
{
    registry.add("Gain",   &make<elements::Gain>);
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

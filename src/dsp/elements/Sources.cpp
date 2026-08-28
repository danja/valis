// src/dsp/elements/Sources.cpp

#include "Common.h"

namespace valis::elements {

/// Audio-rate oscillator. Naive shapes for now: band limiting arrives with the
/// oversampling work, and a saw here is honestly a saw with aliasing.
/// TODO: PolyBLEP the discontinuous shapes.
class Oscillator final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        freqIndex     = controlIndex(type, "frequency");
        shapeIndex    = controlIndex(type, "shape");
        reset();
    }

    void reset() override { phase = 0.0; }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float frequency = std::clamp(controlAt(args, freqIndex, 440.0f),
                                           0.01f, static_cast<float>(sampleRate * 0.45));
        const int shape = static_cast<int>(controlAt(args, shapeIndex, 0.0f) + 0.5f);
        const double increment = frequency / sampleRate;

        float* out = args.audioOut[0];
        for (int i = 0; i < args.numSamples; ++i)
        {
            const auto p = static_cast<float>(phase);

            switch (shape)
            {
                case 1:  out[i] = 2.0f * p - 1.0f;                       break;  // saw
                case 2:  out[i] = p < 0.5f ? 1.0f : -1.0f;               break;  // square
                case 3:  out[i] = 4.0f * std::abs(p - 0.5f) - 1.0f;      break;  // triangle
                default: out[i] = std::sin(6.283185307179586f * p);      break;  // sine
            }

            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    double sampleRate = 44100.0, phase = 0.0;
    int freqIndex = -1, shapeIndex = -1;
};

/// White or pink noise. The pink filter is Paul Kellet's economy version.
class Noise final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        colourIndex = controlIndex(type, "colour");
        reset();
    }

    void reset() override { for (auto& s : pink) s = 0.0f; }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float colour = std::clamp(controlAt(args, colourIndex, 0.0f), 0.0f, 1.0f);
        float* out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            // xorshift: deterministic, so golden-output tests are reproducible.
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            const float white = static_cast<float>(static_cast<int32_t>(seed)) * 4.6566129e-10f;

            pink[0] = 0.99886f * pink[0] + white * 0.0555179f;
            pink[1] = 0.99332f * pink[1] + white * 0.0750759f;
            pink[2] = 0.96900f * pink[2] + white * 0.1538520f;
            const float pinkOut = (pink[0] + pink[1] + pink[2] + white * 0.3104856f) * 0.4f;

            out[i] = white + colour * (pinkOut - white);
        }
    }

private:
    uint32_t seed = 0x9e3779b9u;
    float pink[3] = {};
    int colourIndex = -1;
};

/// Control-rate oscillator. One value per block, which is what a modulation arc
/// carries - audio-rate modulation would need an audio arc instead.
class LFO final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        rateIndex  = controlIndex(type, "rate");
        shapeIndex = controlIndex(type, "shape");
        reset();
    }

    void reset() override { phase = 0.0; held = 0.0f; }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 1)
            return;

        const float frequency = std::max(controlAt(args, rateIndex, 2.0f), 0.001f);
        const int shape = static_cast<int>(controlAt(args, shapeIndex, 0.0f) + 0.5f);

        const auto p = static_cast<float>(phase);
        float value;
        switch (shape)
        {
            case 1:  value = 4.0f * std::abs(p - 0.5f) - 1.0f;   break;  // triangle
            case 2:  value = 2.0f * p - 1.0f;                    break;  // saw
            case 3:  value = p < 0.5f ? 1.0f : -1.0f;            break;  // square
            case 4:                                                      // sample and hold
                if (p < lastPhase)
                {
                    seed = seed * 1664525u + 1013904223u;
                    held = static_cast<float>(seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
                }
                value = held;
                break;
            default: value = std::sin(6.283185307179586f * p);   break;
        }

        lastPhase = p;
        args.controlOut[0] = value;

        phase += static_cast<double>(frequency) * args.numSamples / sampleRate;
        while (phase >= 1.0)
            phase -= 1.0;
    }

private:
    double sampleRate = 44100.0, phase = 0.0;
    float held = 0.0f, lastPhase = 0.0f;
    uint32_t seed = 22222u;
    int rateIndex = -1, shapeIndex = -1;
};

/// The plugin's audio input. The engine fills its output buffer before the
/// graph runs, so this element only has to leave it alone.
class Input final : public DspElement
{
public:
    void process(const ProcessArgs&) noexcept override {}
};

/// The plugin's audio output. Likewise a marker: the engine reads the buffer
/// feeding this node.
class Output final : public DspElement
{
public:
    void process(const ProcessArgs&) noexcept override {}
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerSources(ElementRegistry& registry)
{
    registry.add("Oscillator", &make<elements::Oscillator>);
    registry.add("Noise",      &make<elements::Noise>);
    registry.add("LFO",        &make<elements::LFO>);
    registry.add("Input",      &make<elements::Input>);
    registry.add("Output",     &make<elements::Output>);
}
}  // namespace valis

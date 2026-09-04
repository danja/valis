// src/dsp/elements/Sources.cpp

#include "Common.h"

namespace valis::elements {

/// Band-limited oscillator.
///
/// A naive saw or square steps discontinuously, and a step contains energy at
/// every frequency, so it aliases audibly. PolyBLEP subtracts a polynomial
/// approximation of the band-limited step around each discontinuity, which
/// removes most of that at a cost of a few operations per sample.
///
/// The triangle is the integral of the corrected square, so it inherits the
/// correction rather than needing its own.
class Oscillator final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        freqIndex  = controlIndex(type, "frequency");
        shapeIndex = controlIndex(type, "shape");
        reset();
    }

    void reset() override
    {
        phase = 0.0;
        triangleState = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float frequency = std::clamp(controlAt(args, freqIndex, 440.0f),
                                           0.01f, static_cast<float>(sampleRate * 0.45));
        const int shape = static_cast<int>(controlAt(args, shapeIndex, 0.0f) + 0.5f);
        const double increment = frequency / sampleRate;
        const auto dt = static_cast<float>(increment);

        float* out = args.audioOut[0];
        for (int i = 0; i < args.numSamples; ++i)
        {
            const auto p = static_cast<float>(phase);

            switch (shape)
            {
                case 1:   // saw: one falling step per cycle
                    out[i] = 2.0f * p - 1.0f - polyBlep(p, dt);
                    break;

                case 2:   // square: a rising step at 0 and a falling one at 0.5
                    out[i] = square(p, dt);
                    break;

                case 3:   // triangle: the integral of the corrected square
                {
                    const float s = square(p, dt);
                    triangleState += 4.0f * dt * (s - triangleState * 0.002f);
                    out[i] = std::clamp(triangleState, -1.0f, 1.0f);
                    break;
                }

                default:  // sine: no discontinuity, nothing to correct
                    out[i] = std::sin(6.283185307179586f * p);
                    break;
            }

            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    /// The correction to subtract around a discontinuity at t = 0 (and, by
    /// wrapping, at t = 1). Zero away from the step, so the cost is only paid
    /// on the one or two samples that straddle it.
    static float polyBlep(float t, float dt) noexcept
    {
        if (dt <= 0.0f)
            return 0.0f;

        if (t < dt)                      // just after the step
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }

        if (t > 1.0f - dt)               // just before the next one
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }

        return 0.0f;
    }

    static float square(float p, float dt) noexcept
    {
        float value = p < 0.5f ? 1.0f : -1.0f;
        value += polyBlep(p, dt);

        // The falling step half a cycle later.
        float half = p + 0.5f;
        if (half >= 1.0f)
            half -= 1.0f;
        value -= polyBlep(half, dt);

        return value;
    }

    double sampleRate = 44100.0, phase = 0.0;
    float triangleState = 0.0f;
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

/// Triggered damped sine resonator for bridged-T drum voices.
class TwinTBridge final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate     = rate;
        frequencyIndex = controlIndex(type, "frequency");
        decayIndex     = controlIndex(type, "decay");
        triggerIndex   = controlIndex(type, "trigger");
        velocityIndex  = controlIndex(type, "velocity");
        reset();
    }

    void reset() override
    {
        phase = 0.0;
        amplitude = 0.0f;
        previousTrigger = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float frequency = std::clamp(controlAt(args, frequencyIndex, 55.0f),
                                           10.0f, static_cast<float>(sampleRate * 0.45));
        const float decayMs = std::clamp(controlAt(args, decayIndex, 500.0f), 1.0f, 5000.0f);
        const float trigger = controlAt(args, triggerIndex, -1.0f);
        const bool gate = trigger >= 0.0f ? trigger > 0.5f : args.gate;

        if (gate && previousTrigger <= 0.5f)
        {
            // Use the velocity arc if connected; fall back to the host MIDI velocity.
            const float vel = controlAt(args, velocityIndex, -1.0f);
            amplitude = vel >= 0.0f ? vel : (args.velocity > 0.0f ? args.velocity : 1.0f);
            phase = 0.0;
        }
        previousTrigger = gate ? 1.0f : 0.0f;

        const double increment = frequency / sampleRate;
        const float coeff = timeToCoeff(decayMs, sampleRate);
        float* out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            out[i] = amplitude * std::sin(6.283185307179586 * phase);
            amplitude *= coeff;
            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    double sampleRate = 44100.0, phase = 0.0;
    float amplitude = 0.0f, previousTrigger = 0.0f;
    int frequencyIndex = -1, decayIndex = -1, triggerIndex = -1, velocityIndex = -1;
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

/// Outputs the frequency (Hz) of the most recent MIDI note as a control signal.
/// Holds the last value, so arcs driven by this read a stable pitch between
/// notes. Default is A4 (440 Hz) until the first note arrives.
class MidiPitch final : public DspElement
{
public:
    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 1)
            return;
        // MIDI note → Hz: f = 440 · 2^((n − 69) / 12)
        args.controlOut[0] = 440.0f *
            std::exp2(static_cast<float>(args.noteNumber - 69) * (1.0f / 12.0f));
    }
};

/// Outputs the velocity of the most recent MIDI note-on as a control signal
/// (0–1 normalised). Holds the last value between notes.
class MidiVelocity final : public DspElement
{
public:
    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 1)
            return;
        args.controlOut[0] = args.velocity;
    }
};

/// Routes the host MIDI gate to a control output only when the current note
/// number matches val:note. Lets a circuit wire separate envelope chains per
/// drum hit without any per-voice circuit duplication in the engine.
class NoteGate final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        noteIndex = controlIndex(type, "note");
    }

    void process(const ProcessArgs& args) noexcept override
    {
        const int target = static_cast<int>(controlAt(args, noteIndex, 60.0f));
        float vel = 0.0f;
        if (args.noteVelocities && target >= 0 && target < 128)
        {
            vel = args.noteVelocities[static_cast<std::size_t>(target)];
        }
        else
        {
            vel = (args.gate && args.noteNumber == target) ? args.velocity : 0.0f;
        }

        const bool hit = vel > 0.0f;
        if (args.numControlOut > 0) args.controlOut[0] = hit ? 1.0f : 0.0f;
        if (args.numControlOut > 1) args.controlOut[1] = hit ? vel : 0.0f;
    }

private:
    int noteIndex = -1;
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

/// Digital waveguide single-reed instrument (clarinet model).
///
/// A cylindrical bore (closed at the reed end, open at the bell) is modelled
/// as a delay line of length sampleRate / (2 * frequency). The open end
/// reflects negatively, giving the odd-harmonic spectrum characteristic of a
/// clarinet. The reed junction is a pressure-controlled nonlinear valve:
///
///   delta_p = mouth_pressure - reflected_bore_pressure
///   reed_open = clamp(sqrt(max(0, delta_p)) * k, 0, 1.5)
///   p_new = bore_pressure + reed_open
///
/// Self-oscillates when pressure exceeds the reed's closure threshold.
class Reed final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate   = rate;
        freqIdx      = controlIndex(type, "frequency");
        pressureIdx  = controlIndex(type, "pressure");
        stiffnessIdx = controlIndex(type, "stiffness");
        dampingIdx   = controlIndex(type, "damping");
        buffer.assign(static_cast<std::size_t>(rate / 10.0) + 2, 0.0f);
        writePos    = 0;
        filterState = 0.0f;
    }

    void reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos    = 0;
        filterState = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float freq      = std::clamp(controlAt(args, freqIdx,      220.0f), 20.0f,
                                           static_cast<float>(sampleRate * 0.45));
        const float pressure  = std::clamp(controlAt(args, pressureIdx,   0.5f), 0.0f, 1.0f);
        const float stiffness = std::clamp(controlAt(args, stiffnessIdx,  0.5f), 0.0f, 1.0f);
        const float damping   = std::clamp(controlAt(args, dampingIdx,    0.2f), 0.0f, 1.0f);

        // Round trip for closed-open cylinder: 2L/c = sampleRate / (2 * freq)
        const int N       = std::max(1, static_cast<int>(sampleRate / (2.0 * freq) + 0.5));
        const int bufSize = static_cast<int>(buffer.size());

        const float dampCoeff = damping * 0.9f;
        const float k         = 1.0f + stiffness * 3.0f;   // reed responsiveness
        // Per-sample bore loss: ensures the waveguide decays naturally during
        // release rather than ringing into the next note indefinitely.
        const float loss = 1.0f - damping * 0.004f;

        float* out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            int readPos = writePos - N;
            if (readPos < 0)
                readPos += bufSize;

            // Negative reflection at the open end; loss from bore.
            const float p_back = -buffer[static_cast<std::size_t>(readPos)];

            // One-pole LP in bore (wall losses / mouthpiece damping).
            filterState = dampCoeff * filterState + (1.0f - dampCoeff) * p_back;

            // Reed junction: valve opens proportionally to pressure differential.
            // Closes completely when blowing pressure is absent so that stored
            // bore oscillation decays rather than sustaining without a breath.
            const float delta    = pressure - filterState;
            const float reedOpen = (delta > 0.0f && pressure > 0.01f)
                                 ? std::min(1.5f, std::sqrt(delta) * k)
                                 : 0.0f;

            const float p_new = std::clamp(filterState + reedOpen, -1.0f, 1.0f);
            buffer[static_cast<std::size_t>(writePos)] = p_new * loss;
            out[i] = p_new * 0.25f;

            if (++writePos >= bufSize)
                writePos = 0;
        }
    }

private:
    std::vector<float> buffer;
    int writePos    = 0;
    float filterState = 0.0f;
    double sampleRate = 44100.0;
    int freqIdx = -1, pressureIdx = -1, stiffnessIdx = -1, dampingIdx = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerSources(ElementRegistry& registry)
{
    registry.add("Oscillator",   &make<elements::Oscillator>);
    registry.add("Noise",        &make<elements::Noise>);
    registry.add("TwinTBridge",   &make<elements::TwinTBridge>);
    registry.add("LFO",          &make<elements::LFO>);
    registry.add("MidiPitch",    &make<elements::MidiPitch>);
    registry.add("MidiVelocity", &make<elements::MidiVelocity>);
    registry.add("NoteGate",     &make<elements::NoteGate>);
    registry.add("Reed",         &make<elements::Reed>);
    registry.add("Input",        &make<elements::Input>);
    registry.add("Output",       &make<elements::Output>);
}
}  // namespace valis

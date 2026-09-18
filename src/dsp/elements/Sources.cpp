// src/dsp/elements/Sources.cpp

#include "Common.h"
#include "Waveguide.h"

#include "dsp/Random.h"

#include <cstdint>

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
            const float white = random.bipolar();

            pink[0] = 0.99886f * pink[0] + white * 0.0555179f;
            pink[1] = 0.99332f * pink[1] + white * 0.0750759f;
            pink[2] = 0.96900f * pink[2] + white * 0.1538520f;
            const float pinkOut = (pink[0] + pink[1] + pink[2] + white * 0.3104856f) * 0.4f;

            out[i] = white + colour * (pinkOut - white);
        }
    }

private:
    dsp::Random random;
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

/// Jet-drive waveguide flute.
///
/// A flute is a tube open at both ends, blown by a ribbon of air that the
/// player aims at a sharp edge across the mouth hole. The jet takes time to
/// cross that gap, so the model is two delay lines: the air column, and a
/// shorter jet delay. What the column pushes back deflects the jet, and where
/// the jet lands decides how much air goes into the tube rather than past it.
/// That is the whole instrument.
///
/// The flow into the tube is Uj * tanh((eta - y0) / b), after Verge and Fabre:
/// Uj is the jet's speed, eta how far the acoustic field has deflected it, y0
/// how far it already sits off the edge and b its width. Two things follow from
/// that expression, and both are audible:
///
///   Uj goes with the square root of blowing pressure, which is Bernoulli's,
///   so the instrument gets louder and brighter as it is blown harder rather
///   than saturating at one volume.
///
///   y0 breaks the symmetry. A jet centred on the edge is deflected equally
///   either way and puts out only odd harmonics, which is a hollow, stopped
///   sound. Moving it off centre is what puts the even harmonics in, and it is
///   what a player is doing when they roll the instrument towards or away from
///   themselves. It is the val:offset port.
///
/// See Verge, "Aeroacoustics of confined jets", and Fabre and Hirschberg,
/// <https://ccrma.stanford.edu/~jos/pasp/Flutes_Recorders_Flue_Organ.html>.
class Flute final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate  = rate;
        freqIdx     = controlIndex(type, "frequency");
        pressureIdx = controlIndex(type, "pressure");
        breathIdx   = controlIndex(type, "breath");
        jetIdx      = controlIndex(type, "jet");
        offsetIdx   = controlIndex(type, "offset");
        dampingIdx  = controlIndex(type, "damping");

        const auto longest = static_cast<std::size_t>(rate / 20.0) + 4;
        bore.prepare(longest);
        jet.prepare(longest);
        reset();
    }

    void reset() override
    {
        bore.clear();
        jet.clear();
        loss.clear();
        jetResponse.clear();
        feedbackBlock.clear();
        outputBlock.clear();
        turbulence.seed(0x2545f491u);
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float frequency = std::clamp(controlAt(args, freqIdx, 440.0f), 20.0f,
                                           static_cast<float>(sampleRate * 0.25));
        const float pressure = std::clamp(controlAt(args, pressureIdx, 0.0f), 0.0f, 1.0f);
        const float breath   = std::clamp(controlAt(args, breathIdx, 0.05f), 0.0f, 1.0f);
        const float jetRatio = std::clamp(controlAt(args, jetIdx, 0.5f), 0.35f, 0.65f);
        const float offset   = std::clamp(controlAt(args, offsetIdx, 0.65f), 0.0f, 1.0f);
        const float damping  = std::clamp(controlAt(args, dampingIdx, 0.3f), 0.0f, 1.0f);

        // The top of this range is where the loss filter's own delay stops
        // being predictable from its pole, and the instrument would go most of
        // a semitone sharp. It is already very dark by then.
        loss.setPole(std::clamp(0.4f + 0.45f * damping, 0.05f, 0.85f));

        // The jet delay is inside the feedback loop, so the loop is longer than
        // the air column and the instrument would play sharp. The correction is
        // measured rather than derived: see tests/dsp/FluteTest.cpp, which
        // fails if it drifts.
        const float period = static_cast<float>(sampleRate) / frequency;
        const float omega  = 6.283185307179586f * frequency / static_cast<float>(sampleRate);
        const float tuning = kTuningQuadratic * jetRatio * jetRatio
                           + kTuningLinear * jetRatio + kTuningConstant;

        // The jet does not answer the acoustic field instantly: the
        // perturbation has to grow as the air convects across the gap, and a
        // wide jet cannot follow a short wavelength at all. Modelling that as a
        // lowpass tracking the played pitch is what stops the jet path
        // reinforcing the third and fifth harmonics as strongly as it
        // reinforces the fundamental, which is the difference between a hollow
        // stopped tone and a flute's.
        jetResponse.setPole(std::exp(-6.283185307179586f * kJetCutoff * frequency
                                     / static_cast<float>(sampleRate)));

        // The loss filter's delay comes out of the air column, so damping does
        // not detune the instrument. The jet filter's delay is deliberately
        // left in: it lengthens the jet path relative to the column, which is
        // what stops that path reinforcing the odd harmonics as strongly, and
        // the pitch it costs is given back by the calibration below.
        // What the two fits above leave behind is an error that rises with
        // pitch, because the fixed part of the loop is a larger share of a
        // shorter period. A quadratic in kilohertz takes it out: measured, the
        // instrument holds within a few cents from a bass flute's bottom note
        // to the top of a piccolo's range, where without it the top octave is
        // half a semitone sharp.
        const float kHz = frequency * 0.001f;
        const float residual = std::exp2((kResidualQuadratic * kHz * kHz
                                          + kResidualLinear * kHz
                                          + kResidualConstant) / 1200.0f);

        const float length = std::max(2.0f, period * tuning * residual
                                            - loss.phaseDelay(omega) - 1.0f);
        bore.setDelay(length);
        jet.setDelay(std::max(1.0f, length * jetRatio));

        // Jet speed, and where it sits against the edge before anything
        // deflects it.
        const float velocity = kJetVelocity * std::sqrt(pressure);
        const float bias     = kOffsetRange * offset;
        const float atRest   = waveguide::fastTanh(bias);

        // Moving the jet off the edge flattens the curve it works on, so a
        // player who rolls the instrument in has to blow harder to keep the
        // note. Restoring that slope here is the same compensation, and it
        // leaves the control changing the tone rather than the threshold: it
        // is the difference between a usable knob and one with silence in the
        // middle of its range.
        const float slope = 1.0f - atRest * atRest;   // sech^2, from tanh
        const float compensation = 1.0f / std::sqrt(std::max(slope, 0.2f));

        float* out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            // Round trip: the column's loss, with its steady component removed
            // so the jet keeps swinging about the edge rather than drifting to
            // one side of it.
            const float reflected =
                kLoopGain * feedbackBlock.process(loss.process(bore.last()));

            // The jet's deflection when it reaches the edge, plus the
            // turbulence in the breath, which is what starts the note.
            // The returning wave deflects the jet away from the edge, not
            // towards it. With the sign the other way the loop favours the
            // octave instead of the fundamental.
            const float eta = jetResponse.process(jet.tick(-reflected))
                            + breath * kTurbulence * turbulence.next();

            // How much of the jet goes into the tube. Subtracting its value at
            // rest leaves the flow at zero when the player is not blowing.
            const float flow = velocity * compensation
                             * (waveguide::fastTanh(kJetSensitivity * eta - bias) + atRest);

            out[i] = kOutputLevel
                   * outputBlock.process(bore.tick(flow + kEndReflection * reflected));
        }
    }

private:
    /// Fitted to the sounding pitch measured across the jet range, and then
    /// across the playing range. Both are calibrations rather than
    /// derivations: the loop is nonlinear, and where it settles is not
    /// something the delay lengths alone predict. tests/dsp/FluteTest.cpp
    /// measures both, so they cannot drift unnoticed.
    static constexpr float kTuningQuadratic = -0.0067f;
    static constexpr float kTuningLinear = -0.4253f;
    static constexpr float kTuningConstant = 1.2006f;

    /// And then across the playing range, in kilohertz.
    static constexpr float kResidualQuadratic = 21.85f;
    static constexpr float kResidualLinear    = -10.57f;
    static constexpr float kResidualConstant  = 3.80f;

    /// How hard the acoustic field deflects the jet, and how far the offset
    /// control moves it off the edge, both in jet widths.
    static constexpr float kJetSensitivity = 1.5f;
    static constexpr float kOffsetRange    = 1.2f;

    /// Jet speed at full blowing pressure, in the same units.
    static constexpr float kJetVelocity = 1.0f;

    /// Where the jet stops following the acoustic field, as a multiple of the
    /// played pitch.
    static constexpr float kJetCutoff = 1.5f;

    /// How much of the returning wave goes back down the tube rather than out
    /// of the mouth hole, and how much of the breath's turbulence reaches the
    /// jet.
    static constexpr float kEndReflection = 0.5f;
    static constexpr float kTurbulence    = 0.35f;
    static constexpr float kLoopGain      = 0.985f;
    static constexpr float kOutputLevel   = 0.15f;

    waveguide::Delay bore, jet;
    waveguide::Loss  loss, jetResponse;
    waveguide::DcBlocker feedbackBlock, outputBlock;
    waveguide::Turbulence turbulence;

    double sampleRate = 44100.0;
    int freqIdx = -1, pressureIdx = -1, breathIdx = -1;
    int jetIdx = -1, offsetIdx = -1, dampingIdx = -1;
};

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

/// Bowed string: a cello, a viol, anything played with rosined hair.
///
/// Two travelling waves on a string, with the bow between them. What makes it a
/// bowed string rather than a plucked one is the friction at the bow: the hair
/// grips the string and drags it, the string breaks free and snaps back, and
/// the hair grips it again. That stick and slip cycle is what sustains the note,
/// and it is the whole of the nonlinearity here.
///
/// See Smith, "Physical Audio Signal Processing"
/// (https://ccrma.stanford.edu/~jos/pasp/Bowed_Strings.html) for the waveguide
/// arrangement, and Friedlander and McIntyre for the friction curve the bow
/// table approximates.
///
/// The body is not modelled here. A string on its own is very quiet and very
/// thin, which is what a bridge and a body are for: feed this into a
/// val:ModalBank to get an instrument. examples/cello.ttl does that.
class Bow final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate  = rate;
        driveIn     = audioInIndex(type, "drive");
        freqIdx     = controlIndex(type, "frequency");
        pressureIdx = controlIndex(type, "pressure");
        velocityIdx = controlIndex(type, "velocity");
        positionIdx = controlIndex(type, "position");
        dampingIdx  = controlIndex(type, "damping");

        // Long enough for the lowest note either segment can be asked for.
        const auto capacity = static_cast<std::size_t>(rate / 20.0) + 4;
        toBridge.prepare(capacity);
        toNut.prepare(capacity);
        reset();
    }

    void reset() override
    {
        toBridge.clear();
        toNut.clear();
        loss.clear();
        finger.clear();
        width.clear();
        blocker.clear();
        rosin.seed(0x3c6ef372u);
        bowing = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float frequency = std::clamp(controlAt(args, freqIdx, 220.0f), 20.0f,
                                           static_cast<float>(sampleRate * 0.25));
        const float pressure = std::clamp(controlAt(args, pressureIdx, 0.5f), 0.0f, 1.0f);
        const float speed    = std::clamp(controlAt(args, velocityIdx, 0.4f), 0.0f, 1.0f);
        const float position = std::clamp(controlAt(args, positionIdx, 0.12f), 0.02f, 0.5f);
        const float damping  = std::clamp(controlAt(args, dampingIdx, 0.3f), 0.0f, 1.0f);

        loss.setPole(std::clamp(kPoleBase + kPoleRange * damping, 0.05f, 0.9f));
        finger.setPole(kFingerSoftness);
        width.setPole(kBowWidth);

        // The wave passes each segment once per period, so the two of them come
        // to one period between them. The loss filter's own delay is known in
        // closed form and comes out exactly, which is what keeps the instrument
        // in tune as it is damped; what is left over is calibrated against
        // measurement, as the flute and the clarinet are.
        // See tests/dsp/CelloTest.cpp.
        const float omega    = 6.283185307179586f * frequency / static_cast<float>(sampleRate);
        const float residual = std::exp2((kResidualSlope * frequency + kResidualOffset) / 1200.0f);
        // Every filter in the loop lengthens it, and each of their delays is
        // known in closed form, so all of them come out rather than being
        // absorbed into the calibration.
        const float total    = static_cast<float>(sampleRate) / frequency * residual
                             - loss.phaseDelay(omega)
                             - finger.phaseDelay(omega)
                             - width.phaseDelay(omega)
                             - kLoopLatency;

        const float bridgeLength = std::max(1.0f, total * position);
        const float nutLength    = std::max(1.0f, total - bridgeLength);
        toBridge.setDelay(bridgeLength);
        toNut.setDelay(nutLength);

        // More force widens the band of relative velocity over which the hair
        // holds the string, so a hard bow sticks longer and sounds broader; a
        // light one slips early and sounds thin and glassy.
        const float slope = kSlopeLight - kSlopeRange * pressure;

        // The gate is the player's arm. Bowing is eased in rather than switched
        // on, because an instantaneous bow is a click and not an attack.
        const float target = args.gate ? speed * kBowSpeed : 0.0f;

        // A second string, or anything else, shaking the bow arm. Audio rate
        // rather than control rate, because what is interesting about coupling
        // two of these together happens inside a period, not across a block.
        const float* drive = driveIn >= 0 && driveIn < args.numAudioIn
                           ? args.audioIn[driveIn] : nullptr;
        const float ease   = static_cast<float>(1.0 - std::exp(-1.0 / (kBowEaseMs * 0.001 * sampleRate)));

        float* out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            bowing += ease * (target - bowing);

            // Rosin does not grip evenly, and a real bow is never drawn at a
            // perfectly constant speed. Besides being audible in the tone, the
            // irregularity is what keeps the string out of the neighbouring
            // modes it would otherwise lock into at particular combinations of
            // pitch and bow position.
            const float shake = drive != nullptr ? drive[i] : 0.0f;
            const float hair = bowing * (1.0f + kRosin * rosin.bipolar()) + shake * kDrive;

            const float atBridge = toBridge.last();
            const float atNut    = toNut.last();

            // Both ends invert. The bridge is where the string gives its energy
            // to the body, so that is the lossy end; the nut is nearly rigid.
            const float fromBridge = -kBridgeGain * loss.process(atBridge);
            const float fromNut    = -kNutGain * finger.process(atNut);

            // The bow sees how fast the string is moving under it.
            const float relative = hair - (fromBridge + fromNut);

            // Real bow hair covers a width of the string rather than touching
            // it at a point, and a contact of finite width cannot excite a
            // wavelength shorter than itself. Without that the string locks to
            // a mode a fifth away at the pitches where the bow point happens to
            // fall near a node: see the position sweep in tests/dsp/CelloTest.cpp.
            const float force = width.process(relative * bowTable(relative, slope));

            toBridge.tick(fromNut + force);
            toNut.tick(fromBridge + force);

            // What the bridge passes on. The DC blocker keeps the friction from
            // walking the string off to one side.
            out[i] = blocker.process(atBridge);
        }
    }

private:
    /// How much of the string the hair is holding, as a function of how fast the
    /// string is moving relative to it. One at rest, falling away steeply once
    /// the string breaks free. The exponent is what makes the break sudden,
    /// which is what makes the tone sing rather than hiss.
    static float bowTable(float relative, float slope) noexcept
    {
        const float x = std::abs(relative * slope) + 0.75f;
        const float squared = x * x;
        return std::min(1.0f / (squared * squared), 1.0f);
    }

    /// Damping moves the pole, which darkens the tone as a heavier string or a
    /// duller room would.
    static constexpr float kPoleBase  = 0.05f;
    static constexpr float kPoleRange = 0.55f;

    /// Bow force, mapped to the friction curve's slope. A light bow slips early.
    static constexpr float kSlopeLight = 5.0f;
    static constexpr float kSlopeRange = 4.0f;

    /// What the two ends give back. The bridge loses to the body, the nut
    /// hardly loses at all.
    static constexpr float kBridgeGain = 0.95f;
    static constexpr float kNutGain    = 0.99f;

    /// Bow speed at the top of the control, and how quickly the arm gets there.
    static constexpr float kBowSpeed  = 0.35f;
    static constexpr float kBowEaseMs = 12.0f;

    /// How hard an external signal shakes the bow arm. The friction curve is
    /// steep, so a little goes a long way.
    static constexpr float kDrive = 0.25f;

    /// How uneven the rosin's grip is, as a fraction of bow speed.
    static constexpr float kRosin = 0.04f;

    /// How soft the stopped end is, as a pole.
    static constexpr float kFingerSoftness = 0.45f;

    /// How wide the hair is, as a pole. It smears the node structure the bow
    /// sees, which is what stops the string locking to a neighbouring mode.
    static constexpr float kBowWidth = 0.35f;

    /// One sample of the loop is the arithmetic between the two delay lines.
    static constexpr float kLoopLatency = 1.0f;

    /// What is left of the tuning after the loss filter's delay is taken out.
    /// Uncorrected the string plays sharp by an amount that measures as a
    /// straight line in frequency, from 2.5 cents at the bottom of the cello's
    /// range to 16.7 at the top, so it comes out as cents per hertz.
    /// tests/dsp/CelloTest.cpp measures it at eight pitches.
    static constexpr float kResidualSlope  = 0.0554f;
    static constexpr float kResidualOffset = -0.12f;

    waveguide::Delay toBridge, toNut;
    waveguide::Loss loss;

    /// The bow's contact width, as a lowpass on the force it injects.
    waveguide::Loss width;

    /// The stopped end. A fingertip is soft and lossy, not a rigid termination,
    /// and it damps the high modes a rigid one would let the string sound in.
    waveguide::Loss finger;
    waveguide::DcBlocker blocker;

    /// The unevenness of the rosin, deterministic so a render is reproducible.
    waveguide::Turbulence rosin;

    double sampleRate = 48000.0;
    float  bowing     = 0.0f;
    int driveIn = -1;
    int freqIdx = -1, pressureIdx = -1, velocityIdx = -1, positionIdx = -1, dampingIdx = -1;
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
/// Single-reed waveguide clarinet.
///
/// A cylindrical bore, stopped by the mouthpiece at one end and open at the
/// other. A round trip reflects once with its sign inverted, so the tube holds
/// a quarter wavelength and resonates at odd multiples of it. That is why a
/// clarinet sounds an octave below a flute of the same length, why its even
/// harmonics are weak, and why it overblows to the twelfth rather than the
/// octave. val:Flute is the same machinery with the other boundary condition,
/// and both are built from src/dsp/elements/Waveguide.h.
///
/// The reed is a pressure-controlled valve. The bore's returning pressure works
/// against the player's, and the reed's opening is very nearly a straight line
/// in that difference until it slams against the lay and shuts. That clipped
/// characteristic is the whole nonlinearity, and where the cycle spends its
/// time against the limit is what decides the tone.
///
/// After McIntyre, Schumacher and Woodhouse; see Julius Smith, Physical Audio
/// Signal Processing, <https://ccrma.stanford.edu/~jos/pasp/Clarinet.html>.
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
        breathIdx    = controlIndex(type, "breath");

        bore.prepare(static_cast<std::size_t>(rate / 20.0) + 4);
        reset();
    }

    void reset() override
    {
        bore.clear();
        loss.clear();
        blocker.clear();
        turbulence.seed(0x1f123bb5u);
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float frequency = std::clamp(controlAt(args, freqIdx, 220.0f), 20.0f,
                                           static_cast<float>(sampleRate * 0.25));
        const float pressure  = std::clamp(controlAt(args, pressureIdx, 0.5f), 0.0f, 1.0f);
        const float stiffness = std::clamp(controlAt(args, stiffnessIdx, 0.5f), 0.0f, 1.0f);
        const float damping   = std::clamp(controlAt(args, dampingIdx, 0.2f), 0.0f, 1.0f);
        const float breath    = std::clamp(controlAt(args, breathIdx, 0.02f), 0.0f, 1.0f);

        loss.setPole(std::clamp(kPoleBase + kPoleRange * damping, 0.05f, 0.85f));

        // Stopped at one end and open at the other, so the delay line is half a
        // period. The loss filter's own delay comes out of it, which is what
        // keeps the instrument in tune as it is damped, and the rest is
        // calibrated: see tests/dsp/ClarinetTest.cpp.
        // What the loss filter delays is known in closed form and comes out
        // exactly, which is what keeps the instrument in tune as it is damped.
        // What is left over measures as a straight line in frequency, about
        // 0.079 cents per hertz, and is taken out by the same calibration the
        // flute uses. tests/dsp/ClarinetTest.cpp measures it at eight pitches.
        const float omega    = 6.283185307179586f * frequency / static_cast<float>(sampleRate);
        const float residual = std::exp2((kResidualSlope * frequency + kResidualOffset) / 1200.0f);
        const float length   = 0.5f * static_cast<float>(sampleRate) / frequency * residual
                             - loss.phaseDelay(omega) - kLoopLatency;
        bore.setDelay(std::max(2.0f, length));

        // A soft reed closes further for the same pressure difference, which
        // rounds the tone off; a hard one stays open and sounds brighter.
        const float slope = -(kSlopeSoft - kSlopeRange * stiffness);

        // The reed only works over a narrow band of mouth pressure: below it
        // nothing sounds, and above it the reed is pressed against the lay and
        // stays there. Those two ends are always a factor of two apart, whatever
        // the reed is like, so a control that ran from nothing to the top of the
        // band would be silent over most of its travel. This curve reaches the
        // playable band quickly and then spreads the rest of the control across
        // it, while still passing through zero so an unblown instrument is
        // silent.
        const float blowing = kPressureSpan * std::pow(pressure, kPressureCurve);

        // The turbulence follows the control rather than the mapped pressure,
        // so a player who is not blowing makes no noise either.
        const float noise = breath * pressure;

        float* out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            const float mouth = blowing * (1.0f + noise * turbulence.next());

            // What comes back from the open end: inverted, and duller than it
            // left.
            const float returning = -kLoopGain * loss.process(bore.last());

            // The reed sees the difference between the bore and the player.
            // Its opening follows that difference until it shuts against the
            // lay, and what it lets past joins the player's own pressure.
            const float across  = returning - mouth;
            const float opening = std::clamp(kReedRest + slope * across, -1.0f, 1.0f);

            out[i] = kOutputLevel * blocker.process(bore.tick(mouth + across * opening));
        }
    }

private:
    /// The reed's reflection when nothing is pushing it, and how fast it
    /// closes. The stiffness control moves between a soft reed and a hard one.
    static constexpr float kReedRest   = 0.45f;
    static constexpr float kSlopeSoft  = 0.55f;
    static constexpr float kSlopeRange = 0.2f;

    /// The bore's loss, and the samples the loop spends outside the delay line.
    static constexpr float kPoleBase    = 0.25f;
    static constexpr float kPoleRange   = 0.6f;
    static constexpr float kLoopGain    = 0.995f;

    /// The band of mouth pressure the reed works over.
    static constexpr float kPressureSpan  = 1.20f;
    static constexpr float kPressureCurve = 0.35f;
    static constexpr float kLoopLatency = 1.0f;
    static constexpr float kResidualSlope  = 0.0791f;
    static constexpr float kResidualOffset = -2.43f;
    static constexpr float kOutputLevel = 0.35f;

    waveguide::Delay bore;
    waveguide::Loss  loss;
    waveguide::DcBlocker blocker;

    /// The unevenness of the rosin, deterministic so a render is reproducible.
    waveguide::Turbulence rosin;
    waveguide::Turbulence turbulence;

    double sampleRate = 44100.0;
    int freqIdx = -1, pressureIdx = -1, stiffnessIdx = -1, dampingIdx = -1, breathIdx = -1;
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
    registry.add("Bow",           &make<elements::Bow>);
    registry.add("Reed",         &make<elements::Reed>);
    registry.add("Flute",        &make<elements::Flute>);
    registry.add("Input",        &make<elements::Input>);
    registry.add("Output",       &make<elements::Output>);
}
}  // namespace valis

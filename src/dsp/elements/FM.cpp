// src/dsp/elements/FM.cpp
//
// The two elements a DX7-style FM voice is built from: a phase-modulated sine
// operator, and the rate and level envelope that shapes it. An algorithm is not
// an element. It is the arcs between operators, written in the circuit.

#include "Common.h"

#include <array>

namespace valis::elements {

namespace {

constexpr float kTwoPi = 6.283185307179586f;

/// The DX7's level scale: 0 to 99, linear in decibels, 99 being unity and each
/// step below it about 0.75 dB down. Operator output level and envelope level
/// both use it, which is why a level of 0 is silence and 1 is already 74 dB down.
inline float dxLevelToGain(float level) noexcept
{
    if (level <= 0.0f)
        return 0.0f;

    // Continuous into silence: without the ramp the gain would step from 0 to
    // the value at 1 as the level crossed it.
    return std::exp2((std::min(level, 99.0f) - 99.0f) * 0.125f) * std::min(level, 1.0f);
}

}  // namespace

/// A sine oscillator whose phase is driven by an audio input.
///
/// This is what the DX7 calls FM, and strictly it is phase modulation: the
/// input is added to the phase, not to the frequency. The two give the same
/// spectra, and phase modulation does not drift in pitch when the modulator has
/// a DC offset. See Chowning, "The Synthesis of Complex Audio Spectra by Means
/// of Frequency Modulation" (https://en.wikipedia.org/wiki/Frequency_modulation_synthesis).
///
/// The modulation index is the modulator's amplitude times `depth`, so the
/// modulator's envelope is its index envelope, which is how the DX7 gets a
/// bright attack that mellows. Several arcs on `pm` sum, which is how an
/// operator with more than one modulator is wired.
///
/// Feedback is internal and mixes the operator's last two outputs, as the DX7's
/// does. The averaging is what keeps it from turning to noise at high settings.
class FMOperator final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        frequencyIdx  = controlIndex(type, "frequency");
        ratioIdx      = controlIndex(type, "ratio");
        fixedIdx      = controlIndex(type, "fixed");
        detuneIdx     = controlIndex(type, "detune");
        levelIdx      = controlIndex(type, "level");
        ampIdx        = controlIndex(type, "amp");
        depthIdx      = controlIndex(type, "depth");
        feedbackIdx   = controlIndex(type, "feedback");
        reset();
    }

    void reset() override
    {
        phase = 0.0;
        y1 = y2 = 0.0f;
        primed = false;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float fixed = controlAt(args, fixedIdx, 0.0f);
        const float hz = fixed > 0.0f
            ? fixed
            : controlAt(args, frequencyIdx, 440.0f) * controlAt(args, ratioIdx, 1.0f)
                * std::exp2(controlAt(args, detuneIdx, 0.0f) * (1.0f / 1200.0f));

        const double increment = std::clamp(static_cast<double>(hz) / sampleRate, 0.0, 0.49);

        const float target = std::clamp(controlAt(args, ampIdx, 1.0f), 0.0f, 1.0f)
                           * dxLevelToGain(controlAt(args, levelIdx, 99.0f));
        if (! primed)
        {
            gain = target;
            primed = true;
        }

        const int n = args.numSamples;
        const float step = n > 0 ? (target - gain) / static_cast<float>(n) : 0.0f;

        const float depth = controlAt(args, depthIdx, kDefaultDepth) * (1.0f / kTwoPi);
        const float feedback = std::clamp(controlAt(args, feedbackIdx, 0.0f), 0.0f, 7.0f)
                             * kFeedbackPerStep * 0.5f * (1.0f / kTwoPi);

        // An unconnected pm reads silence, so there is no branch on it.
        const float* pm = args.numAudioIn > 0 ? args.audioIn[0] : nullptr;
        float* out = args.audioOut[0];

        for (int i = 0; i < n; ++i)
        {
            float p = static_cast<float>(phase)
                    + (pm != nullptr ? pm[i] * depth : 0.0f)
                    + feedback * (y1 + y2);
            p -= std::floor(p);

            gain += step;
            const float y = std::sin(kTwoPi * p) * gain;
            y2 = y1;
            y1 = y;
            out[i] = y;

            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    /// Radians of modulation from a modulator at full level. The DX7's reaches
    /// roughly four turns, which is where an operator stops sounding like a
    /// sine and starts sounding like noise.
    static constexpr float kDefaultDepth = 12.566371f;

    /// Radians of feedback per step of the 0 to 7 control, so 7 is about pi,
    /// where the operator is nearly a sawtooth.
    static constexpr float kFeedbackPerStep = 0.45f;

    double sampleRate = 44100.0, phase = 0.0;
    float y1 = 0.0f, y2 = 0.0f, gain = 0.0f;
    bool primed = false;
    int frequencyIdx = -1, ratioIdx = -1, fixedIdx = -1, detuneIdx = -1;
    int levelIdx = -1, ampIdx = -1, depthIdx = -1, feedbackIdx = -1;
};

/// Six operators in one element, with a modulation matrix between them.
///
/// A loop through several operators has to be sample accurate: each output must
/// reach the next operator's phase on the following sample, not on the
/// following block. Elements process a whole buffer at a time, so a loop made of
/// separate val:FMOperator elements cannot do that (val:UnitDelay delays a
/// slice and a sample), and the loop has to live inside one element.
///
/// Operators are evaluated from 6 down to 1, as on the DX7. A modulator numbered
/// higher than its target has already been computed for this sample and is used
/// as it is; a modulator numbered lower, or the operator itself, is used as it
/// was on the previous sample. So the usual wiring costs nothing, and a loop is
/// closed by one sample of delay on the arc that runs against the order.
///
/// m<d><s> is how much operator s modulates operator d, as a fraction of
/// val:depth. On the diagonal it is the DX7's feedback, 0 to 7, which averages
/// the operator's last two outputs as val:FMOperator does.
/// The DX7's 32 algorithms, as which operators modulate which and which are heard.
///
/// The machine describes each as six bytes, one per operator from 6 down to 1, naming the
/// bus the operator reads, the bus it writes and whether it adds to that bus or replaces
/// it (the layout is the one the open-source msfa engine uses). Reading the bytes through
/// a simulation of the buses gives the diagram: an operator's modulators are whatever was
/// last written to the bus it reads. The table is decoded once, when the first element is
/// prepared.
struct DxAlgorithm
{
    unsigned modulators[6] = {};   ///< bit s set: operator s+1 modulates this one
    unsigned carriers = 0;         ///< bit n set: operator n+1 is heard
    int feedbackOp = -1;           ///< the operator that reads the feedback, 0 to 5
    int feedbackFrom = -1;         ///< the operator it reads from; the same one for self feedback
};

const std::array<DxAlgorithm, 32>& dxAlgorithms()
{
    static const unsigned char kBytes[32][6] = {
        {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14},
        {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14}, {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14},
        {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14},
        {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14},
        {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14},
        {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
        {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14},
        {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14},
        {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
        {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14},
        {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14}, {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14},
        {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
        {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04},
        {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14},
        {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
        {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04},
    };

    static const std::array<DxAlgorithm, 32> table = []
    {
        std::array<DxAlgorithm, 32> decoded;
        for (std::size_t a = 0; a < decoded.size(); ++a)
        {
            unsigned bus[3] = {};   // who has written each bus; bus 0 is the output
            int feedbackIn = -1, feedbackOut = -1;

            for (int step = 0; step < 6; ++step)
            {
                const int op = 5 - step;
                const unsigned flags = kBytes[a][step];
                const int readBus  = static_cast<int>(flags >> 4) & 3;
                const int writeBus = static_cast<int>(flags) & 3;
                const bool add     = (flags & 0x04) != 0;

                decoded[a].modulators[op] = readBus != 0 ? bus[readBus] : 0u;

                if (writeBus == 0)
                    decoded[a].carriers |= 1u << op;
                else
                    bus[writeBus] = add && bus[writeBus] != 0 ? bus[writeBus] | (1u << op) : 1u << op;

                if (flags & 0x40) feedbackIn  = op;
                if (flags & 0x80) feedbackOut = op;   // the last operator to write it is the source
            }
            decoded[a].feedbackOp   = feedbackIn;
            decoded[a].feedbackFrom = feedbackOut;
        }
        return decoded;
    }();
    return table;
}

class FMMatrix final : public DspElement
{
public:
    static constexpr int kOps = 6;

    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate   = rate;
        frequencyIdx = controlIndex(type, "frequency");
        depthIdx     = controlIndex(type, "depth");
        algorithmIdx = controlIndex(type, "algorithm");
        feedbackIdx  = controlIndex(type, "feedback");
        dxAlgorithms();   // decoded here, on the message thread, the first time
        for (int i = 0; i < kOps; ++i)
        {
            const std::string n = std::to_string(i + 1);
            ratioIdx[i]   = controlIndex(type, "ratio" + n);
            detuneIdx[i]  = controlIndex(type, "detune" + n);
            levelIdx[i]   = controlIndex(type, "level" + n);
            ampIdx[i]     = controlIndex(type, "amp" + n);
            carrierIdx[i] = controlIndex(type, "carrier" + n);
            for (int j = 0; j < kOps; ++j)
                matrixIdx[i][j] = controlIndex(type, "m" + n + std::to_string(j + 1));
        }
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < kOps; ++i)
        {
            phase[i] = 0.0;
            y1[i] = y2[i] = gain[i] = 0.0f;
        }
        primed = false;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const int n = args.numSamples;
        const float frequency = controlAt(args, frequencyIdx, 440.0f);
        const float depth = controlAt(args, depthIdx, kDefaultDepth) * (1.0f / kTwoPi);

        // The block's settings, read once. Only non-zero entries of the matrix
        // are kept, so an unused connection costs nothing per sample.
        double increment[kOps];
        float target[kOps], step[kOps], carrier[kOps];
        bool active[kOps];
        int count[kOps];
        int source[kOps][kOps];
        float weight[kOps][kOps];
        bool average[kOps][kOps] = {};   // feedback: the source's last two outputs, not its last

        // A chosen algorithm replaces the matrix and the carriers; the feedback port
        // is the DX7's single feedback control.
        const int algorithm = std::clamp(static_cast<int>(controlAt(args, algorithmIdx, 0.0f) + 0.5f), 0, 32);
        const DxAlgorithm* fixed = algorithm > 0 ? &dxAlgorithms()[static_cast<std::size_t>(algorithm - 1)] : nullptr;
        const float feedbackAmount = std::clamp(controlAt(args, feedbackIdx, 0.0f), 0.0f, 7.0f)
                                   * kFeedbackPerStep * 0.5f * (1.0f / kTwoPi);

        for (int i = 0; i < kOps; ++i)
        {
            const float hz = frequency * controlAt(args, ratioIdx[i], 1.0f)
                           * std::exp2(controlAt(args, detuneIdx[i], 0.0f) * (1.0f / 1200.0f));
            increment[i] = std::clamp(static_cast<double>(hz) / sampleRate, 0.0, 0.49);

            target[i] = std::clamp(controlAt(args, ampIdx[i], 1.0f), 0.0f, 1.0f)
                      * dxLevelToGain(controlAt(args, levelIdx[i], 99.0f));
            if (! primed)
                gain[i] = target[i];
            step[i] = n > 0 ? (target[i] - gain[i]) / static_cast<float>(n) : 0.0f;
            carrier[i] = fixed != nullptr ? ((fixed->carriers >> i) & 1u ? 1.0f : 0.0f)
                                          : std::clamp(controlAt(args, carrierIdx[i], 0.0f), 0.0f, 1.0f);

            // Below -140 dB the operator is off, so it can be skipped outright.
            if (target[i] < 1.0e-7f && gain[i] < 1.0e-7f)
            {
                gain[i] = 0.0f;
                step[i] = 0.0f;
            }
            active[i] = gain[i] > 0.0f || target[i] > 0.0f;

            count[i] = 0;
            for (int j = 0; j < kOps; ++j)
            {
                float amount;
                if (fixed == nullptr)
                    amount = controlAt(args, matrixIdx[i][j], 0.0f);
                else if (i == j)
                    amount = fixed->feedbackOp == i && fixed->feedbackFrom == i ? 1.0f : 0.0f;
                else
                    amount = ((fixed->modulators[i] >> j) & 1u) != 0 ? 1.0f : 0.0f;

                if (amount <= 0.0f)
                    continue;

                source[i][count[i]] = j;
                weight[i][count[i]] = i == j
                    ? (fixed != nullptr ? feedbackAmount
                                        : std::min(amount, 7.0f) * kFeedbackPerStep * 0.5f * (1.0f / kTwoPi))
                    : amount * depth;
                ++count[i];
            }

            // A loop through several operators, as in algorithms 4 and 6: the operator
            // that closes it reads the source's output with the feedback control's weight.
            if (fixed != nullptr && fixed->feedbackOp == i && fixed->feedbackFrom != i
                && fixed->feedbackFrom >= 0 && feedbackAmount > 0.0f)
            {
                source[i][count[i]]  = fixed->feedbackFrom;
                weight[i][count[i]]  = feedbackAmount;
                average[i][count[i]] = true;
                ++count[i];
            }
        }
        primed = true;

        float* out = args.audioOut[0];
        float current[kOps];

        for (int t = 0; t < n; ++t)
        {
            float mix = 0.0f;

            for (int i = kOps - 1; i >= 0; --i)
            {
                gain[i] += step[i];

                // A silent operator contributes nothing, so an idle voice
                // costs almost none of its sines.
                float y = 0.0f;
                if (active[i])
                {
                    float p = static_cast<float>(phase[i]);
                    for (int k = 0; k < count[i]; ++k)
                    {
                        const int j = source[i][k];
                        if (j == i)
                            p += weight[i][k] * (y1[i] + y2[i]);
                        else if (average[i][k])
                            p += weight[i][k] * (y1[j] + y2[j]);
                        else
                            p += weight[i][k] * (j > i ? current[j] : y1[j]);
                    }
                    p -= std::floor(p);
                    y = std::sin(kTwoPi * p) * gain[i];
                }

                current[i] = y;
                mix += carrier[i] * y;

                phase[i] += increment[i];
                if (phase[i] >= 1.0)
                    phase[i] -= 1.0;
            }

            for (int i = 0; i < kOps; ++i)
            {
                y2[i] = y1[i];
                y1[i] = current[i];
            }
            out[t] = mix;
        }
    }

private:
    static constexpr float kDefaultDepth = 12.566371f;
    static constexpr float kFeedbackPerStep = 0.45f;

    double sampleRate = 44100.0;
    double phase[kOps] = {};
    float y1[kOps] = {}, y2[kOps] = {}, gain[kOps] = {};
    bool primed = false;
    int frequencyIdx = -1, depthIdx = -1, algorithmIdx = -1, feedbackIdx = -1;
    int ratioIdx[kOps], detuneIdx[kOps], levelIdx[kOps], ampIdx[kOps], carrierIdx[kOps];
    int matrixIdx[kOps][kOps];
};

/// The DX7's envelope: four rates and four levels.
///
/// On key down the level moves to L1 at R1, then to L2 at R2, then to L3 at R3
/// and holds there. On key up it moves to L4 at R4. Levels and rates are the
/// DX7's 0 to 99, so a patch written down for the machine can be typed in.
///
/// The level moves in the DX7's logarithmic scale (see dxLevelToGain), at a
/// speed that doubles about every nine steps of rate, so a given rate takes the
/// same time whatever distance it has to cover. That is how it differs from an
/// ADSR, whose segments take the same time whatever the distance.
///
/// The curve is an approximation: the timings are in the right range for the
/// machine and have not been matched to it step for step. Rising segments run
/// faster than falling ones, as they do on the DX7.
class RateLevelEnvelope final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate  = rate;
        gateIdx     = controlIndex(type, "gate");
        velocityIdx = controlIndex(type, "velocity");
        velsensIdx  = controlIndex(type, "velsens");
        rateScaleIdx  = controlIndex(type, "ratescale");
        breakpointIdx = controlIndex(type, "breakpoint");
        leftDepthIdx  = controlIndex(type, "leftdepth");
        rightDepthIdx = controlIndex(type, "rightdepth");
        pitchRangeIdx = controlIndex(type, "pitchrange");
        for (int i = 0; i < 4; ++i)
        {
            const std::string n = std::to_string(i + 1);
            rateIdx[i]  = controlIndex(type, "r" + n);
            levelIdx[i] = controlIndex(type, "l" + n);
        }
        reset();
    }

    void reset() override
    {
        level = 0.0f;
        stage = 3;
        wasGated = false;
        primed = false;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 1)
            return;

        // Keyboard rate scaling: the higher the key, the faster every segment, up to about
        // 27 steps of rate at the top of the keyboard at full depth. The machine's own
        // curve is stepped in groups of notes; this one is smooth.
        const float keyPosition = std::clamp((static_cast<float>(args.noteNumber) - 21.0f) / 87.0f, 0.0f, 1.0f);
        const float rateBoost   = std::clamp(controlAt(args, rateScaleIdx, 0.0f), 0.0f, 7.0f) * keyPosition * (27.0f / 7.0f);

        float rates[4], levels[4];
        for (int i = 0; i < 4; ++i)
        {
            rates[i]  = std::clamp(controlAt(args, rateIdx[i], 99.0f) + rateBoost, 0.0f, 99.0f);
            levels[i] = std::clamp(controlAt(args, levelIdx[i], i == 3 ? 0.0f : 99.0f), 0.0f, 99.0f);
        }

        // The envelope rests at L4, which is where the DX7 starts it.
        if (! primed)
        {
            level = levels[3];
            primed = true;
        }

        const float gateCtrl = controlAt(args, gateIdx, -1.0f);
        const bool gateNow = gateCtrl >= 0.0f ? gateCtrl >= 0.5f : args.gate;

        if (gateNow && ! wasGated)
            stage = 0;
        else if (! gateNow && wasGated)
            stage = 3;
        wasGated = gateNow;

        // Spend the slice's time on segments, carrying what is left over into
        // the next one so a fast rate can cross several in one slice.
        float remaining = static_cast<float>(args.numSamples / sampleRate);
        for (int guard = 0; guard < 4 && remaining > 0.0f; ++guard)
        {
            if (stage > 3)
                break;

            const float target = levels[stage];
            const float diff = target - level;
            const float speed = speedFor(rates[stage], diff > 0.0f);
            const float move = speed * remaining;

            if (std::abs(diff) > move)
            {
                level += diff > 0.0f ? move : -move;
                break;
            }

            level = target;
            remaining -= speed > 0.0f ? std::abs(diff) / speed : remaining;

            if (stage == 2)
                break;   // sustains until the key comes up

            ++stage;     // past 3 is finished
        }

        const float vel = std::clamp(controlAt(args, velocityIdx, 1.0f), 0.0f, 1.0f);
        const float velocityGain = std::exp2(std::clamp(controlAt(args, velsensIdx, 0.0f), 0.0f, 7.0f)
                                             * (vel - 1.0f));

        // Keyboard level scaling: steps of level added or taken away, growing in a straight
        // line with the distance from the breakpoint, full depth at three octaves.
        const float away = static_cast<float>(args.noteNumber) - std::clamp(controlAt(args, breakpointIdx, 60.0f), 0.0f, 127.0f);
        const float depth = away < 0.0f ? controlAt(args, leftDepthIdx, 0.0f) : controlAt(args, rightDepthIdx, 0.0f);
        const float scaling = std::clamp(depth, -99.0f, 99.0f) * std::min(std::abs(away) / 36.0f, 1.0f);

        args.controlOut[0] = dxLevelToGain(std::clamp(level + scaling, 0.0f, 99.0f)) * velocityGain;

        // The same contour as a frequency ratio, for the pitch envelope: a level of 50
        // is no change, 99 is `pitchrange` octaves up and 0 is as many down. Connect it
        // through a val:ControlMultiply to the note's frequency. With the range at its
        // default of 0 the ratio is always 1.
        if (args.numControlOut >= 2)
        {
            const float octaves = std::clamp(controlAt(args, pitchRangeIdx, 0.0f), 0.0f, 4.0f);
            args.controlOut[1] = std::exp2(octaves * (level - 50.0f) * (1.0f / 50.0f));
        }
    }

private:
    /// Steps of level per second. Rate 0 takes about forty seconds to cross the
    /// scale and rate 99 well under a hundredth of a second.
    static float speedFor(float rate, bool rising) noexcept
    {
        return 2.5f * std::exp2(rate * (1.0f / 9.0f)) * (rising ? 3.0f : 1.0f);
    }

    double sampleRate = 44100.0;
    float level = 0.0f;
    int stage = 3;
    bool wasGated = false, primed = false;
    int gateIdx = -1, velocityIdx = -1, velsensIdx = -1;
    int rateScaleIdx = -1, breakpointIdx = -1, leftDepthIdx = -1, rightDepthIdx = -1, pitchRangeIdx = -1;
    int rateIdx[4] = {-1, -1, -1, -1};
    int levelIdx[4] = {-1, -1, -1, -1};
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerFm(ElementRegistry& registry)
{
    registry.add("FMOperator",         &make<elements::FMOperator>);
    registry.add("FMMatrix",           &make<elements::FMMatrix>);
    registry.add("RateLevelEnvelope",  &make<elements::RateLevelEnvelope>);
}
}  // namespace valis

// src/dsp/elements/Utility.cpp

#include "Common.h"

#include "dsp/Random.h"

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

/// Sums whatever arrives at its inputs (mono 'in' or stereo 'left'/'right').
/// The engine has already added the arcs together into those buffers.
class Mixer final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        inIdx    = audioInIndex(type, "in");
        leftIn   = audioInIndex(type, "left");
        rightIn  = audioInIndex(type, "right");
        outIdx   = audioOutIndex(type, "out");
        leftOut  = audioOutIndex(type, "left");
        rightOut = audioOutIndex(type, "right");
    }

    void process(const ProcessArgs& args) noexcept override
    {
        const int n = args.numSamples;

        const float* monoIn  = inIdx >= 0 && inIdx < args.numAudioIn ? args.audioIn[inIdx] : nullptr;
        const float* stereoL = leftIn >= 0 && leftIn < args.numAudioIn ? args.audioIn[leftIn] : nullptr;
        const float* stereoR = rightIn >= 0 && rightIn < args.numAudioIn ? args.audioIn[rightIn] : nullptr;

        if (outIdx >= 0 && outIdx < args.numAudioOut)
        {
            float* out = args.audioOut[outIdx];
            for (int i = 0; i < n; ++i)
                out[i] = monoIn != nullptr ? monoIn[i]
                                           : 0.5f * ((stereoL != nullptr ? stereoL[i] : 0.0f)
                                                   + (stereoR != nullptr ? stereoR[i] : 0.0f));
        }

        if (leftOut >= 0 && leftOut < args.numAudioOut)
        {
            float* lOut = args.audioOut[leftOut];
            if (stereoL != nullptr)
                std::copy(stereoL, stereoL + n, lOut);
            else if (monoIn != nullptr)
                std::copy(monoIn, monoIn + n, lOut);
            else
                std::fill(lOut, lOut + n, 0.0f);
        }

        if (rightOut >= 0 && rightOut < args.numAudioOut)
        {
            float* rOut = args.audioOut[rightOut];
            if (stereoR != nullptr)
                std::copy(stereoR, stereoR + n, rOut);
            else if (monoIn != nullptr)
                std::copy(monoIn, monoIn + n, rOut);
            else
                std::fill(rOut, rOut + n, 0.0f);
        }
    }

private:
    int inIdx = -1, leftIn = -1, rightIn = -1;
    int outIdx = -1, leftOut = -1, rightOut = -1;
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

class ControlMultiply final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        aIndex = controlIndex(type, "a");
        bIndex = controlIndex(type, "b");
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 1)
            return;

        args.controlOut[0] = controlAt(args, aIndex, 0.0f) * controlAt(args, bIndex, 0.0f);
    }

private:
    int aIndex = -1, bIndex = -1;
};

/// Two-way control switch. `select` picks `a` when it is off and `b` when it is
/// on, which turns one binary choice in the Controls view into any two values a
/// circuit needs, rather than only 0 and 1.
///
/// `thru` repeats the resolved switch position, so one switch can reach several
/// destinations: chain it into another Select's `select`, or into a
/// val:ControlMultiply to gate a control path off.
class Select final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        aIndex      = controlIndex(type, "a");
        bIndex      = controlIndex(type, "b");
        selectIndex = controlIndex(type, "select");
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 2)
            return;

        const bool on = controlAt(args, selectIndex, 0.0f) > 0.5f;

        args.controlOut[0] = on ? controlAt(args, bIndex, 1.0f)
                                : controlAt(args, aIndex, 0.0f);
        args.controlOut[1] = on ? 1.0f : 0.0f;
    }

private:
    int aIndex = -1, bIndex = -1, selectIndex = -1;
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

/// Equal-power stereo panner. Maps mono audio in to left, right, and combined out.
class Pan final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        panIndex = controlIndex(type, "pan");
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float pan = std::clamp(controlAt(args, panIndex, 0.0f), -1.0f, 1.0f);
        const float angle = (pan + 1.0f) * 0.7853981633974483f;   // (pan + 1) * pi/4
        const float gainL = std::cos(angle);
        const float gainR = std::sin(angle);

        const float* in = args.audioIn[0];
        float* outMono = args.audioOut[0];
        float* outL = args.numAudioOut > 1 ? args.audioOut[1] : nullptr;
        float* outR = args.numAudioOut > 2 ? args.audioOut[2] : nullptr;

        for (int i = 0; i < args.numSamples; ++i)
        {
            const float sampleL = in[i] * gainL;
            const float sampleR = in[i] * gainR;
            outMono[i] = (sampleL + sampleR) * 0.70710678f;
            if (outL) outL[i] = sampleL;
            if (outR) outR[i] = sampleR;
        }
    }

private:
    int panIndex = -1;
};

/// Control gate choke element.
class Choke final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        gateIndex  = controlIndex(type, "gate");
        chokeIndex = controlIndex(type, "choke");
        reset();
    }

    void reset() override
    {
        choked = false;
        prevGate = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 1)
            return;

        const float gateIn  = controlAt(args, gateIndex, 0.0f);
        const float chokeIn = controlAt(args, chokeIndex, 0.0f);

        if (gateIn > 0.5f && prevGate <= 0.5f)
            choked = false;
        prevGate = gateIn;

        if (chokeIn > 0.5f)
            choked = true;

        args.controlOut[0] = choked ? 0.0f : (gateIn > 0.5f ? 1.0f : 0.0f);
    }

private:
    int gateIndex = -1, chokeIndex = -1;
    bool choked = false;
    float prevGate = 0.0f;
};

/// Signal generator for test signals and circuit development.
class SignalGenerator final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        freqIndex  = controlIndex(type, "frequency");
        ampIndex   = controlIndex(type, "amplitude");
        shapeIndex = controlIndex(type, "shape");
        reset();
    }

    void reset() override { phase = 0.0; noise.seed(kNoiseSeed); }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float freq  = std::clamp(controlAt(args, freqIndex, 440.0f), 1.0f, static_cast<float>(sampleRate * 0.45));
        const float amp   = std::clamp(controlAt(args, ampIndex, 0.5f), 0.0f, 1.0f);
        const int   shape = static_cast<int>(controlAt(args, shapeIndex, 0.0f) + 0.5f);

        const double inc = freq / sampleRate;
        float* out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            float s = 0.0f;
            const float p = static_cast<float>(phase);
            switch (shape)
            {
                case 0: s = std::sin(6.283185307179586 * phase); break;   // Sine
                case 1: s = p < 0.5f ? 1.0f : -1.0f; break;                // Square
                case 2: s = 2.0f * p - 1.0f; break;                        // Saw
                case 3: s = 4.0f * std::abs(p - 0.5f) - 1.0f; break;        // Triangle
                case 4: s = noise.bipolar(); break;                        // White noise
                case 5: s = (phase < inc) ? 1.0f : 0.0f; break;  // Impulse train at frequency
                default: s = std::sin(6.283185307179586 * phase); break;
            }
            out[i] = s * amp;
            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
        }
    }

private:
    /// Per instance and restored by reset(), so two generators in one circuit
    /// are independent and a render of the same circuit is reproducible.
    static constexpr uint32_t kNoiseSeed = 0x9e3779b9u;

    double sampleRate = 44100.0, phase = 0.0;
    dsp::Random noise{kNoiseSeed};
    int freqIndex = -1, ampIndex = -1, shapeIndex = -1;
};

/// Waveform and level monitor element.
class Oscilloscope final : public DspElement
{
public:
    void prepare(const ElementType&, double rate, int) override
    {
        sampleRate = rate;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        float peak = 0.0f;
        double sumSq = 0.0;
        int zeroCrossings = 0;

        for (int i = 0; i < args.numSamples; ++i)
        {
            out[i] = in[i];
            const float absS = std::abs(in[i]);
            if (absS > peak) peak = absS;
            sumSq += in[i] * in[i];

            if (i > 0 && ((in[i - 1] <= 0.0f && in[i] > 0.0f) || (in[i - 1] >= 0.0f && in[i] < 0.0f)))
                zeroCrossings++;
        }

        const float rms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(std::max(args.numSamples, 1))));
        const float estFreq = (zeroCrossings * 0.5f * static_cast<float>(sampleRate)) / static_cast<float>(std::max(args.numSamples, 1));

        if (args.numControlOut > 0) args.controlOut[0] = peak;
        if (args.numControlOut > 1) args.controlOut[1] = rms;
        if (args.numControlOut > 2) args.controlOut[2] = estFreq;
    }

private:
    double sampleRate = 44100.0;
};

/// Frequency analyzer meter element.
class FreqAnalyzer final : public DspElement
{
public:
    void prepare(const ElementType&, double rate, int) override
    {
        sampleRate = rate;
        stateLow = 0.0f;
        stateHigh = 0.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        double lowSum = 0.0, midSum = 0.0, highSum = 0.0;
        double weightedFreqSum = 0.0, totalMagSum = 1e-9;

        const float alphaLow  = std::exp(static_cast<float>(-2.0 * 3.14159265358979 * 500.0 / sampleRate));
        const float alphaHigh = std::exp(static_cast<float>(-2.0 * 3.14159265358979 * 4000.0 / sampleRate));

        for (int i = 0; i < args.numSamples; ++i)
        {
            const float s = in[i];
            out[i] = s;

            stateLow  = alphaLow * stateLow + (1.0f - alphaLow) * s;
            stateHigh = alphaHigh * stateHigh + (1.0f - alphaHigh) * s;
            const float sMid = s - stateLow - (s - stateHigh);

            lowSum  += stateLow * stateLow;
            midSum  += sMid * sMid;
            highSum += (s - stateHigh) * (s - stateHigh);

            const float mag = std::abs(s);
            weightedFreqSum += mag;
            totalMagSum += mag;
        }

        const float lowRms  = static_cast<float>(std::sqrt(lowSum  / static_cast<double>(std::max(args.numSamples, 1))));
        const float midRms  = static_cast<float>(std::sqrt(midSum  / static_cast<double>(std::max(args.numSamples, 1))));
        const float highRms = static_cast<float>(std::sqrt(highSum / static_cast<double>(std::max(args.numSamples, 1))));
        const float centroid = static_cast<float>(weightedFreqSum / totalMagSum) * 1000.0f;

        if (args.numControlOut > 0) args.controlOut[0] = lowRms;
        if (args.numControlOut > 1) args.controlOut[1] = midRms;
        if (args.numControlOut > 2) args.controlOut[2] = highRms;
        if (args.numControlOut > 3) args.controlOut[3] = centroid;
    }

private:
    double sampleRate = 44100.0;
    float stateLow = 0.0f, stateHigh = 0.0f;
};

/// Turns a control gate into MIDI note events on the plugin's output.
///
/// The circuit's answer to the survey's cases whose output is events rather than
/// audio. Nothing here is audio: the gate rises, a note goes out; it falls, the
/// note is released. Patched behind val:Oscilloscope's frequency estimate it
/// makes an audio-to-MIDI converter out of parts that already existed.
class NoteOut final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        gateIndex     = controlIndex(type, "gate");
        pitchIndex    = controlIndex(type, "pitch");
        velocityIndex = controlIndex(type, "velocity");
        channelIndex  = controlIndex(type, "channel");
        reset();
    }

    /// A held note must not survive a relocation: the host would be left with a
    /// note it can never turn off.
    void reset() override
    {
        wasOpen = false;
        sounding = -1;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        const bool open = controlAt(args, gateIndex, 0.0f) > 0.5f;
        if (open == wasOpen)
            return;

        wasOpen = open;

        const int channel = std::clamp(static_cast<int>(controlAt(args, channelIndex, 1.0f) + 0.5f),
                                       1, 16);

        if (open)
        {
            // The pitch is read once, when the note starts, so bending the
            // control while the note is held does not silently retune a note
            // the host has already been told about.
            const float hz = std::max(controlAt(args, pitchIndex, 440.0f), 1.0f);
            sounding = std::clamp(static_cast<int>(std::lround(
                           69.0 + 12.0 * std::log2(static_cast<double>(hz) / 440.0))), 0, 127);

            ElementEvent event;
            event.noteOn   = true;
            event.note     = sounding;
            event.velocity = std::clamp(controlAt(args, velocityIndex, 0.8f), 0.0f, 1.0f);
            event.channel  = channel;
            args.emit(event);
        }
        else if (sounding >= 0)
        {
            ElementEvent event;
            event.noteOn  = false;
            event.note    = sounding;
            event.channel = channel;
            args.emit(event);
            sounding = -1;
        }
    }

private:
    int gateIndex = -1, pitchIndex = -1, velocityIndex = -1, channelIndex = -1;
    bool wasOpen = false;
    int  sounding = -1;   ///< the note the host currently believes is held
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerUtility(ElementRegistry& registry)
{
    registry.add("Gain",            &make<elements::Gain>);
    registry.add("VCA",             &make<elements::VCA>);
    registry.add("Scale",           &make<elements::Scale>);
    registry.add("ControlMultiply", &make<elements::ControlMultiply>);
    registry.add("Select",          &make<elements::Select>);
    registry.add("Delay",           &make<elements::Delay>);
    registry.add("Mixer",           &make<elements::Mixer>);
    registry.add("DryWet",          &make<elements::DryWet>);
    registry.add("Pan",             &make<elements::Pan>);
    registry.add("Choke",           &make<elements::Choke>);
    registry.add("SignalGenerator", &make<elements::SignalGenerator>);
    registry.add("Oscilloscope",    &make<elements::Oscilloscope>);
    registry.add("FreqAnalyzer",    &make<elements::FreqAnalyzer>);
    registry.add("NoteOut",         &make<elements::NoteOut>);
}

// The one place that knows the whole set. makeDefaultRegistry's keys and the
// ontology's val:implementation values must match exactly; a test asserts it.
void registerSources(ElementRegistry&);
void registerFilters(ElementRegistry&);
void registerTransfers(ElementRegistry&);
void registerDynamics(ElementRegistry&);
void registerGranular(ElementRegistry&);
void registerSpectralElements(ElementRegistry&);

ElementRegistry makeDefaultRegistry()
{
    ElementRegistry registry;
    registerSources(registry);
    registerFilters(registry);
    registerTransfers(registry);
    registerDynamics(registry);
    registerGranular(registry);
    registerSpectralElements(registry);
    registerUtility(registry);
    return registry;
}
}  // namespace valis

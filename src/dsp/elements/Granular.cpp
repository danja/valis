// src/dsp/elements/Granular.cpp
//
// Granular synthesis, and the two control sources a granular instrument needs:
// the host timeline, and a MIDI note expressed as an interval rather than a
// frequency.
//
// File loading happens in setOption, which the engine calls on the message
// thread while the element is still outside the running graph. process() only
// reads the buffer that left behind.

#include "Common.h"

#include "dsp/GrainPool.h"
#include "dsp/Random.h"
#include "dsp/SampleFile.h"


#include <array>
#include <cstdint>
#include <string>

namespace valis::elements {

using dsp::SampleFile;
using dsp::SampleExpectation;

/// Granular synthesiser and processor.
///
/// One circular buffer holds the material: live audio arrives at the audio
/// input and is written at the write head unless the element is frozen, and
/// val:file replaces the contents with a sound file. Grains are short windowed
/// reads from that buffer, each with its own position, playback rate, window
/// and stereo placement, overlapping into a continuous texture.
///
/// See Roads, "Microsound" (MIT Press, 2001) for the technique, and
/// https://pichenettes.github.io/mutable-instruments-documentation/modules/clouds/
/// for the live-buffer arrangement this follows.
class Granulator final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;

        positionIndex    = controlIndex(type, "position");
        sizeIndex        = controlIndex(type, "size");
        densityIndex     = controlIndex(type, "density");
        pitchIndex       = controlIndex(type, "pitch");
        sprayIndex       = controlIndex(type, "spray");
        jitterIndex      = controlIndex(type, "jitter");
        pitchJitterIndex = controlIndex(type, "pitchJitter");
        shapeIndex       = controlIndex(type, "shape");
        spreadIndex      = controlIndex(type, "spread");
        reverseIndex     = controlIndex(type, "reverse");
        freezeIndex      = controlIndex(type, "freeze");
        triggerIndex     = controlIndex(type, "trigger");
        scanIndex        = controlIndex(type, "scan");

        outIndex   = audioOutIndex(type, "out");
        leftIndex  = audioOutIndex(type, "left");
        rightIndex = audioOutIndex(type, "right");

        allocateBuffer(bufferSeconds);
        reset();
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error) override
    {
        if (key == "seconds")
        {
            const double seconds = parseSeconds(value);
            if (seconds <= 0.0)
            {
                error = "expected a positive number of seconds, got '" + std::string(value) + "'";
                return false;
            }

            allocateBuffer(std::clamp(seconds, 0.25, 60.0));
            reset();
            return true;
        }

        if (const auto declared = expectation.setDeclared(key, value, error);
            declared != SampleExpectation::Declared::No)
        {
            return declared == SampleExpectation::Declared::Yes
                && expectation.check(source, error);
        }

        if (key == "file")
            return loadFile(value, error);

        return true;
    }

    void reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);

        // val:seconds may have shrunk the buffer since the file was read, so
        // what is restored is whatever still fits.
        length = std::min(fileLength, static_cast<int>(buffer.size()));
        if (length > 0)
            std::copy(fileContent.begin(), fileContent.begin() + length, buffer.begin());

        writeHead     = 0;
        untilNextGrain = 0.0;
        scanOffset     = 0.0;
        lastTrigger    = 0.0f;
        lastInterval   = 0.0f;
        grains.seed(0x5eed1234u);
        grains.clear();
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        float* outMono  = outIndex   >= 0 && outIndex   < args.numAudioOut ? args.audioOut[outIndex]   : nullptr;
        float* outLeft  = leftIndex  >= 0 && leftIndex  < args.numAudioOut ? args.audioOut[leftIndex]  : nullptr;
        float* outRight = rightIndex >= 0 && rightIndex < args.numAudioOut ? args.audioOut[rightIndex] : nullptr;

        const int n = args.numSamples;

        // Every declared output is written on every block, silence included:
        // an output left untouched would hand the next reader whatever the
        // buffer happened to hold.
        if (outMono  != nullptr) std::fill(outMono,  outMono  + n, 0.0f);
        if (outLeft  != nullptr) std::fill(outLeft,  outLeft  + n, 0.0f);
        if (outRight != nullptr) std::fill(outRight, outRight + n, 0.0f);

        if (buffer.empty())
            return;

        const float  positionCtl = std::clamp(controlAt(args, positionIndex, 0.0f), 0.0f, 1.0f);
        const float  sizeMs      = std::clamp(controlAt(args, sizeIndex, 120.0f), 1.0f, 2000.0f);
        const float  density     = std::clamp(controlAt(args, densityIndex, 20.0f), 0.01f, 200.0f);
        const float  pitch       = std::clamp(controlAt(args, pitchIndex, 0.0f), -48.0f, 48.0f);
        const float  spray       = std::clamp(controlAt(args, sprayIndex, 0.0f), 0.0f, 1.0f);
        const float  jitter      = std::clamp(controlAt(args, jitterIndex, 0.0f), 0.0f, 1.0f);
        const float  pitchJitter = std::clamp(controlAt(args, pitchJitterIndex, 0.0f), 0.0f, 24.0f);
        const float  shape       = std::clamp(controlAt(args, shapeIndex, 0.5f), 0.0f, 1.0f);
        const float  spread      = std::clamp(controlAt(args, spreadIndex, 0.5f), 0.0f, 1.0f);
        const float  reverse     = std::clamp(controlAt(args, reverseIndex, 0.0f), 0.0f, 1.0f);
        const float  freezeCtl   = controlAt(args, freezeIndex, 0.0f);
        const float  trigger     = controlAt(args, triggerIndex, -1.0f);
        const float  scan        = std::clamp(controlAt(args, scanIndex, 0.0f), -4.0f, 4.0f);

        const bool frozen = freezeCtl > 0.5f;

        const int   capacity    = static_cast<int>(buffer.size());
        const float grainLength = std::max(2.0f, sizeMs * 0.001f * static_cast<float>(sampleRate));
        const float fade        = std::max(0.002f, shape * 0.5f);

        // Overlapping grains sum, so a dense cloud is louder than a sparse one
        // by roughly the square root of the overlap count. Taking that back out
        // keeps density usable as a texture control rather than a level one.
        const float overlap = std::max(1.0f, density * sizeMs * 0.001f);
        const float makeup  = 1.0f / std::sqrt(overlap);

        const bool  externalTrigger = trigger >= -0.5f;
        const float interval = static_cast<float>(sampleRate) / density;

        // A countdown left over from a slower density would keep the element
        // quiet long after the control moved, so a change of density shortens
        // the current wait to what it now asks for. Only a change does this:
        // clamping on every block would cut the long half off jitter, and would
        // make the result depend on where the block boundaries fell.
        if (std::abs(interval - lastInterval) > 1.0e-6f * std::max(1.0f, interval))
        {
            untilNextGrain = std::min(untilNextGrain, static_cast<double>(interval));
            lastInterval = interval;
        }

        // Nothing wired to the input points at the engine's shared silence, and
        // recording that would erase material val:file had loaded. An input
        // that is merely quiet still records, which is what freezing is for.
        const float* in = args.numAudioIn > 0 ? args.audioIn[0] : nullptr;
        if (in != nullptr && in == args.silence)
            in = nullptr;

        // The read point drifts at `scan` buffer lengths per second. It advances
        // per sample rather than per block, so a grain spawned part-way through
        // a block starts where it would have at any other buffer size.
        const double scanStep = static_cast<double>(scan)
                              * static_cast<double>(std::max(length, 1)) / sampleRate;

        // One grain per rising edge when an external clock drives the element.
        // The edge is tracked in both modes, so a circuit that switches between
        // them by moving the resting value of the arc behaves the same however
        // it got there.
        if (externalTrigger && trigger > 0.5f && lastTrigger <= 0.5f)
            grains.spawn(grainSpec(positionCtl, grainLength, pitch, spray,
                                           pitchJitter, spread, reverse));

        lastTrigger = trigger;

        for (int i = 0; i < n; ++i)
        {
            if (! frozen && in != nullptr)
            {
                buffer[static_cast<std::size_t>(writeHead)] = in[i];
                if (++writeHead >= capacity)
                    writeHead = 0;
                if (length < capacity)
                    ++length;
            }

            // Free-running grain onsets. The countdown lives in a member and is
            // driven by the sample stream, never by the index within this
            // block, so the result does not change with the host's buffer size.
            if (! externalTrigger)
            {
                untilNextGrain -= 1.0;
                while (untilNextGrain <= 0.0)
                {
                    grains.spawn(grainSpec(positionCtl, grainLength, pitch, spray,
                                           pitchJitter, spread, reverse));
                    const float wobble = 1.0f + jitter * grains.generator().bipolar() * 0.9f;
                    untilNextGrain += static_cast<double>(std::max(1.0f, interval * wobble));
                }
            }

            scanOffset += scanStep;

            float left = 0.0f, right = 0.0f;

            grains.mix(left, right, fade, static_cast<double>(length),
                       [this](double position) { return read(position); });

            left  *= makeup;
            right *= makeup;

            if (outLeft  != nullptr) outLeft[i]  = left;
            if (outRight != nullptr) outRight[i] = right;
            if (outMono  != nullptr) outMono[i]  = 0.5f * (left + right);
        }

        // scanOffset is deliberately left unwrapped. spawn() reduces it modulo
        // the buffer length where it is used, and wrapping it here would change
        // grain positions while the buffer is still filling, since the length it
        // would be reduced by is not the length it is later used with. A double
        // drifting at the fastest rate this port allows keeps sub-sample
        // precision for far longer than any session.
    }

private:
    /// Where in the buffer a grain starts, and how it is to be played. The
    /// position is measured forward from the write head: with a file loaded the
    /// head sits at the start of the file, and while recording it is the present
    /// moment.
    dsp::GrainSpec grainSpec(float position, float grainLength, float pitch, float spray,
                             float pitchJitter, float spread, float reverse) const noexcept
    {
        dsp::GrainSpec spec;
        spec.span          = static_cast<double>(length);
        spec.origin        = static_cast<double>(writeHead) + scanOffset;
        spec.position      = position;
        spec.lengthSamples = grainLength;
        spec.pitch         = pitch;
        spec.spray         = spray;
        spec.pitchJitter   = pitchJitter;
        spec.spread        = spread;
        spec.reverse       = reverse;
        return spec;
    }

    /// Catmull-Rom interpolation. Grains play at arbitrary rates, so linear
    /// interpolation would put audible high-frequency loss on every
    /// transposition.
    float read(double position) const noexcept
    {
        if (length <= 0)
            return 0.0f;

        int i1 = static_cast<int>(position);
        const float f = static_cast<float>(position - static_cast<double>(i1));
        if (i1 >= length)
            i1 = 0;

        // The caller keeps the read position inside the buffer, so wrapping the
        // three neighbours is a comparison rather than a division.
        int i0 = i1 - 1; if (i0 < 0)       i0 += length;
        int i2 = i1 + 1; if (i2 >= length) i2 -= length;
        int i3 = i2 + 1; if (i3 >= length) i3 -= length;

        const float y0 = buffer[static_cast<std::size_t>(i0)];
        const float y1 = buffer[static_cast<std::size_t>(i1)];
        const float y2 = buffer[static_cast<std::size_t>(i2)];
        const float y3 = buffer[static_cast<std::size_t>(i3)];

        const float a = 0.5f * (-y0 + 3.0f * y1 - 3.0f * y2 + y3);
        const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c = 0.5f * (-y0 + y2);

        return ((a * f + b) * f + c) * f + y1;
    }

    void allocateBuffer(double seconds)
    {
        bufferSeconds = seconds;
        buffer.assign(static_cast<std::size_t>(seconds * sampleRate) + 4, 0.0f);
    }

    static double parseSeconds(std::string_view value)
    {
        try
        {
            return std::stod(std::string(value));
        }
        catch (...)
        {
            return 0.0;
        }
    }

    /// Message thread only. The samples become the buffer's contents, and are
    /// kept so reset() can restore them after the buffer is cleared.
    bool loadFile(std::string_view path, std::string& error)
    {
        SampleFile file;
        if (! file.load(path, sampleRate, error))
            return false;

        source = file;
        if (! expectation.check(source, error))
            return false;

        fileContent = std::move(file.samples);
        fileLength  = static_cast<int>(fileContent.size());

        if (fileLength + 4 > static_cast<int>(buffer.size()))
            allocateBuffer(static_cast<double>(fileLength) / sampleRate + 0.1);

        reset();
        return true;
    }

    /// What the circuit declared about its sound file, and what the file
    /// actually was. `source.samples` is not kept: fileContent holds it.
    SampleExpectation expectation;
    SampleFile        source;

    std::vector<float> buffer;      ///< the circular recording buffer
    std::vector<float> fileContent; ///< what val:file loaded, for reset()

    /// A cloud of sixty-four is more than the ear resolves, and the pool is
    /// shared machinery: see src/dsp/GrainPool.h.
    dsp::GrainPool<64> grains;

    double sampleRate    = 44100.0;
    double bufferSeconds = 8.0;
    double untilNextGrain = 0.0;
    double scanOffset    = 0.0;

    int   writeHead  = 0;
    int   length     = 0;   ///< how much of the buffer holds material
    int   fileLength = 0;
    float lastTrigger  = 0.0f;
    float lastInterval = 0.0f;

    int positionIndex = -1, sizeIndex = -1, densityIndex = -1, pitchIndex = -1;
    int sprayIndex = -1, jitterIndex = -1, pitchJitterIndex = -1, shapeIndex = -1;
    int spreadIndex = -1, reverseIndex = -1, freezeIndex = -1, triggerIndex = -1;
    int scanIndex = -1;
    int outIndex = -1, leftIndex = -1, rightIndex = -1;
};

/// Plays a sound file named by val:file.
///
/// The file is read on the message thread when the circuit is installed. The
/// element is what a circuit points at when it wants a sample: the Controls
/// view draws one of these as a file slot with a Load button, so the sample can
/// be changed without editing the document.
///
/// Feed it into a val:Granulator's audio input to granulate a recording, or use
/// it on its own as a one-shot player.
class SampleLoad final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate   = rate;
        triggerIndex = controlIndex(type, "trigger");
        speedIndex   = controlIndex(type, "speed");
        startIndex   = controlIndex(type, "start");
        loopIndex    = controlIndex(type, "loop");
        reset();
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error) override
    {
        if (const auto declared = expectation.setDeclared(key, value, error);
            declared != SampleExpectation::Declared::No)
        {
            return declared == SampleExpectation::Declared::Yes
                && expectation.check(source, error);
        }

        if (key != "file")
            return true;

        SampleFile file;
        if (! file.load(value, sampleRate, error))
            return false;

        source = file;
        if (! expectation.check(source, error))
            return false;

        samples = std::move(file.samples);
        reset();
        return true;
    }

    void reset() override
    {
        readPos     = 0.0;
        playing     = ! samples.empty();
        lastTrigger = 0.0f;
        triggered   = false;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        float* out = args.audioOut[0];
        const int n = args.numSamples;
        std::fill(out, out + n, 0.0f);

        const auto length = static_cast<int>(samples.size());
        if (length <= 0)
            return;

        const float speed   = std::clamp(controlAt(args, speedIndex, 1.0f), -4.0f, 4.0f);
        const float startCtl= std::clamp(controlAt(args, startIndex, 0.0f), 0.0f, 1.0f);
        const bool  looping = controlAt(args, loopIndex, 1.0f) > 0.5f;
        const float trigger = controlAt(args, triggerIndex, -1.0f);

        // -1 leaves the element free-running, which for a looping player means
        // it simply plays. Any other value makes each rising edge a restart.
        if (trigger >= -0.5f)
        {
            // A connected trigger owns playback. Until its first rising edge the
            // element is silent, rather than playing itself once the moment the
            // circuit loads: a drum machine with seventeen voices would
            // otherwise sound all of them at the downbeat.
            if (! triggered)
                playing = false;

            if (trigger > 0.5f && lastTrigger <= 0.5f)
            {
                readPos = static_cast<double>(startCtl) * static_cast<double>(length);
                playing   = true;
                triggered = true;
            }
            lastTrigger = trigger;
        }

        if (! playing)
            return;

        for (int i = 0; i < n; ++i)
        {
            out[i] = read(readPos, length);
            readPos += static_cast<double>(speed);

            if (readPos >= static_cast<double>(length) || readPos < 0.0)
            {
                if (! looping)
                {
                    playing = false;
                    break;
                }

                while (readPos >= static_cast<double>(length)) readPos -= static_cast<double>(length);
                while (readPos < 0.0)                          readPos += static_cast<double>(length);
            }
        }
    }

private:
    /// Linear interpolation is enough here: unlike a grain, a player usually
    /// runs at or near its recorded rate.
    float read(double position, int length) const noexcept
    {
        auto i0 = static_cast<int>(position);
        if (i0 < 0 || i0 >= length)
            i0 = 0;
        int i1 = i0 + 1;
        if (i1 >= length)
            i1 = 0;

        const auto f = static_cast<float>(position - std::floor(position));
        return samples[static_cast<std::size_t>(i0)]
             + f * (samples[static_cast<std::size_t>(i1)] - samples[static_cast<std::size_t>(i0)]);
    }

    /// What the circuit declared about its sound file, and what the file was.
    SampleExpectation expectation;
    SampleFile        source;

    std::vector<float> samples;
    double sampleRate = 44100.0;
    double readPos    = 0.0;
    bool   playing    = false;
    bool   triggered  = false;   ///< whether a connected trigger has fired yet
    float  lastTrigger = 0.0f;
    int triggerIndex = -1, speedIndex = -1, startIndex = -1, loopIndex = -1;
};

/// The host timeline as control signals.
///
/// `division` is measured in quarter notes, so 0.25 is a sixteenth and 4 is a
/// bar of four-four. When the host is stopped the phase free-runs at the same
/// rate, so a circuit still behaves sensibly in the standalone app, where there
/// is no transport at all.
class Transport final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        divisionIndex = controlIndex(type, "division");
        reset();
    }

    void reset() override
    {
        freePhase   = 0.0;
        lastPulse   = 0;
        wasPlaying  = false;
        havePulse   = false;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 5)
            return;

        const double division = std::clamp(static_cast<double>(controlAt(args, divisionIndex, 1.0f)),
                                           1.0 / 64.0, 64.0);
        const double tempo = std::clamp(args.transport.tempoBpm, 1.0, 999.0);
        const double rate  = tempo / 60.0 / division;

        double phase   = 0.0;
        bool   pulsed  = false;

        if (args.transport.playing)
        {
            const double beats = args.transport.ppqPosition / division;
            const auto   index = static_cast<std::int64_t>(std::floor(beats));

            phase = beats - std::floor(beats);

            // The first block after the transport starts is a pulse, so a
            // circuit driven by it begins at once rather than staying silent
            // until the next division boundary comes round.
            pulsed    = ! wasPlaying || ! havePulse || index != lastPulse;
            lastPulse = index;
            havePulse = true;
            freePhase = phase;
        }
        else
        {
            freePhase += rate * static_cast<double>(args.numSamples) / sampleRate;
            if (freePhase >= 1.0)
            {
                freePhase -= std::floor(freePhase);
                pulsed = true;
            }
            phase     = freePhase;
            havePulse = false;
        }

        wasPlaying = args.transport.playing;

        args.controlOut[0] = args.transport.playing ? 1.0f : 0.0f;
        args.controlOut[1] = static_cast<float>(tempo);
        args.controlOut[2] = static_cast<float>(phase);
        args.controlOut[3] = pulsed ? 1.0f : 0.0f;
        args.controlOut[4] = static_cast<float>(rate);
    }

private:
    double sampleRate = 44100.0;
    double freePhase  = 0.0;
    std::int64_t lastPulse = 0;
    bool wasPlaying = false, havePulse = false;
    int divisionIndex = -1;
};

/// The most recent MIDI note as an interval from a root note, in semitones and
/// as a playback ratio. val:MidiPitch answers "what frequency?", which is what
/// an oscillator needs; a sampler or granulator needs "how far from the root?".
class MidiInterval final : public DspElement
{
public:
    void prepare(const ElementType& type, double, int) override
    {
        rootIndex = controlIndex(type, "root");
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numControlOut < 2)
            return;

        const float root = std::clamp(controlAt(args, rootIndex, 60.0f), 0.0f, 127.0f);
        const float semitones = static_cast<float>(args.noteNumber) - root;

        args.controlOut[0] = semitones;
        args.controlOut[1] = std::pow(2.0f, semitones / 12.0f);
    }

private:
    int rootIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerGranular(ElementRegistry& registry)
{
    registry.add("Granulator",   &make<elements::Granulator>);
    registry.add("SampleLoad",   &make<elements::SampleLoad>);
    registry.add("Transport",    &make<elements::Transport>);
    registry.add("MidiInterval", &make<elements::MidiInterval>);
}
}  // namespace valis

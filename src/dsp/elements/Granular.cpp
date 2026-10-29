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

#include "dsp/GrainCloud.h"
#include "dsp/GrainPool.h"
#include "dsp/Random.h"
#include "dsp/Scale.h"
#include "dsp/SampleFile.h"


#include <array>
#include <cstdint>
#include <string>

namespace valis::elements {

using dsp::SampleFile;
using dsp::SampleExpectation;

/// Where grain material comes from: the buffer, and the options that fill it. A sound
/// file is read on the message thread, in setOption, while the element is still outside
/// the running graph; process() only reads what that left behind.
struct GrainSource
{
    GrainMaterial material;

    void prepare(double rate)
    {
        material.sampleRate = rate;
        material.allocate(material.bufferSeconds);
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error)
    {
        if (key == "seconds")
        {
            const double seconds = parseSeconds(value);
            if (seconds <= 0.0)
            {
                error = "expected a positive number of seconds, got '" + std::string(value) + "'";
                return false;
            }

            material.allocate(std::clamp(seconds, 0.25, 60.0));
            material.restore();
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

    void reset() { material.restore(); }

private:
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
        if (! file.load(path, material.sampleRate, error))
            return false;

        source = file;
        if (! expectation.check(source, error))
            return false;

        material.fileContent = std::move(file.samples);
        material.fileLength  = static_cast<int>(material.fileContent.size());

        if (material.fileLength + 4 > static_cast<int>(material.buffer.size()))
            material.allocate(static_cast<double>(material.fileLength) / material.sampleRate + 0.1);

        material.restore();
        return true;
    }

    /// What the circuit declared about its sound file, and what the file
    /// actually was. `source.samples` is not kept: fileContent holds it.
    SampleExpectation expectation;
    SampleFile        source;
};

/// The input a granulator records from, or null if nothing is wired to it. Nothing wired
/// points at the engine's shared silence, and recording that would erase material val:file
/// had loaded. An input that is merely quiet still records, which is what freezing is for.
inline const float* recordedInput(const ProcessArgs& args) noexcept
{
    const float* in = args.numAudioIn > 0 ? args.audioIn[0] : nullptr;
    return in != nullptr && in == args.silence ? nullptr : in;
}

/// Granular synthesiser and processor.
///
/// One circular buffer holds the material: live audio arrives at the audio
/// input and is written at the write head unless the element is frozen, and
/// val:file replaces the contents with a sound file. Grains are short windowed
/// reads from that buffer, each with its own position, playback rate, window
/// and stereo placement, overlapping into a continuous texture.
///
/// This is val:GrainBuffer and val:GrainCloud in one element: the material and the
/// grains are the same code the pair uses, here run together so that a grain is started
/// against the buffer as it stands at that very sample.
///
/// See Roads, "Microsound" (MIT Press, 2001) for the technique, and
/// https://pichenettes.github.io/mutable-instruments-documentation/modules/clouds/
/// for the live-buffer arrangement this follows.
class Granulator final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        freezeIndex = controlIndex(type, "freeze");
        core.prepare(type, rate);
        content.prepare(rate);
        reset();
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error) override
    {
        return content.setOption(key, value, error);
    }

    void reset() override
    {
        content.reset();
        core.reset();
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        auto& material = content.material;
        const bool frozen = controlAt(args, freezeIndex, 0.0f) > 0.5f;
        const float* in = recordedInput(args);
        const bool recording = ! frozen && in != nullptr;
        material.recording = recording;

        core.run(args, material, recording, [&](int i) noexcept
                 {
                     if (recording)
                         material.write(in[i]);
                 });
    }

private:
    GrainSource content;
    GrainCloudCore core;
    int freezeIndex = -1;
};

/// The material half of a granulator: a buffer that records its audio input, or holds a
/// sound file, and offers itself on a buffer port to any number of val:GrainCloud elements.
/// Several clouds on one buffer, each with its own position and pitch, is the reason to
/// have the two apart.
class GrainBuffer final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        freezeIndex = controlIndex(type, "freeze");
        content.prepare(rate);
        reset();
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error) override
    {
        return content.setOption(key, value, error);
    }

    SharedBuffer* bufferOut(std::string_view port) override
    {
        return port == "buffer" ? &content.material : nullptr;
    }

    void reset() override { content.reset(); }

    void process(const ProcessArgs& args) noexcept override
    {
        auto& material = content.material;
        const bool frozen = controlAt(args, freezeIndex, 0.0f) > 0.5f;
        const float* in = recordedInput(args);
        const bool recording = ! frozen && in != nullptr;
        material.recording = recording;

        if (recording && ! material.buffer.empty())
            for (int i = 0; i < args.numSamples; ++i)
                material.write(in[i]);
    }

private:
    GrainSource content;
    int freezeIndex = -1;
};

/// The grain half of a granulator: grains cut from the material its buffer port was given,
/// by a val:GrainBuffer. It runs after the buffer in each block. Unconnected, it is silent.
class GrainCloud final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        core.prepare(type, rate);
        reset();
    }

    bool bufferIn(std::string_view port, SharedBuffer* shared, std::string& error) override
    {
        if (port != "buffer")
        {
            error = "has no buffer input '" + std::string(port) + "'";
            return false;
        }

        material = dynamic_cast<GrainMaterial*>(shared);
        if (material == nullptr)
        {
            error = "the buffer input takes a val:GrainBuffer's buffer";
            return false;
        }
        return true;
    }

    void reset() override { core.reset(); }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        auto& from = material != nullptr ? *material : nothing;
        core.run(args, from, from.recording, [](int) noexcept {});
    }

private:
    GrainCloudCore core;
    GrainMaterial* material = nullptr;
    GrainMaterial  nothing;   ///< what an unconnected cloud reads: no material at all
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
        chokeIndex   = controlIndex(type, "choke");
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
        lastChoke   = 0.0f;
        fading      = false;
        fadeGain    = 1.0f;
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
        bool restarted = false;

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
                restarted = true;

                // A restart overrides a stop that is still fading out.
                fading   = false;
                fadeGain = 1.0f;
            }
            lastTrigger = trigger;
        }

        // A rise on choke cuts the sound off, over a couple of milliseconds so the
        // cut does not click. A sound restarted in the same block plays, as the
        // trigger is read first.
        const float choke = controlAt(args, chokeIndex, 0.0f);
        if (choke > 0.5f && lastChoke <= 0.5f && playing && ! restarted)
            fading = true;
        lastChoke = choke;

        if (! playing)
            return;

        const float fadeStep = static_cast<float>(1.0 / (0.002 * sampleRate));

        for (int i = 0; i < n; ++i)
        {
            out[i] = read(readPos, length);
            if (fading)
            {
                fadeGain -= fadeStep;
                if (fadeGain <= 0.0f)
                {
                    out[i] = 0.0f;
                    playing  = false;
                    fading   = false;
                    fadeGain = 1.0f;
                    break;
                }
                out[i] *= fadeGain;
            }
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
    float  lastTrigger = 0.0f, lastChoke = 0.0f, fadeGain = 1.0f;
    bool   fading = false;
    int triggerIndex = -1, speedIndex = -1, startIndex = -1, loopIndex = -1, chokeIndex = -1;
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
        if (args.numControlOut > 5)
            args.controlOut[5] = static_cast<float>(1000.0 / rate);
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
    registry.add("GrainBuffer", &make<elements::GrainBuffer>);
    registry.add("GrainCloud",  &make<elements::GrainCloud>);
    registry.add("Granulator",   &make<elements::Granulator>);
    registry.add("SampleLoad",   &make<elements::SampleLoad>);
    registry.add("Transport",    &make<elements::Transport>);
    registry.add("MidiInterval", &make<elements::MidiInterval>);
}
}  // namespace valis

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

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <cstdint>
#include <string>

namespace valis::elements {

/// Deterministic per-element noise. A granulator seeded from the clock would
/// render differently every time, which would make offline audio tests
/// worthless; seeding in reset() makes a render reproducible.
class Xorshift
{
public:
    void seed(std::uint32_t value) noexcept { state = value != 0 ? value : 0x9e3779b9u; }

    /// Uniform in [0, 1).
    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
    }

    /// Uniform in [-1, 1).
    float bipolar() noexcept { return next() * 2.0f - 1.0f; }

private:
    std::uint32_t state = 0x9e3779b9u;
};

/// Reads a sound file into a mono buffer at the engine's rate. Message thread
/// only: it opens a file and allocates, so it belongs nowhere near process().
///
/// Shared by val:Granulator and val:SampleLoad, which differ in what they do
/// with the samples, not in how they get them.
struct SampleFile
{
    std::vector<float> samples;
    std::string name;      ///< the file's own name, for the Controls view

    bool load(std::string_view path, double sampleRate, std::string& error)
    {
        const auto file = resolve(path);
        if (! file.existsAsFile())
        {
            error = "no such file: " + file.getFullPathName().toStdString();
            return false;
        }

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
        {
            error = "unreadable audio file: " + file.getFullPathName().toStdString();
            return false;
        }

        const auto frames = static_cast<int>(std::min<juce::int64>(
            reader->lengthInSamples, static_cast<juce::int64>(sampleRate * 60.0)));
        if (frames <= 0)
        {
            error = "audio file is empty: " + file.getFullPathName().toStdString();
            return false;
        }

        juce::AudioBuffer<float> source(static_cast<int>(reader->numChannels), frames);
        reader->read(&source, 0, frames, 0, true, true);

        // The circuit model is mono; a stereo file is summed rather than having
        // one of its channels dropped.
        std::vector<float> mono(static_cast<std::size_t>(frames), 0.0f);
        for (int c = 0; c < source.getNumChannels(); ++c)
        {
            const float* channel = source.getReadPointer(c);
            for (int i = 0; i < frames; ++i)
                mono[static_cast<std::size_t>(i)] += channel[i];
        }
        if (source.getNumChannels() > 1)
            for (auto& sample : mono)
                sample /= static_cast<float>(source.getNumChannels());

        const double ratio = reader->sampleRate > 0.0 ? reader->sampleRate / sampleRate : 1.0;
        const auto resampled = static_cast<int>(static_cast<double>(frames) / ratio);

        samples.assign(static_cast<std::size_t>(std::max(resampled, 1)), 0.0f);
        for (int i = 0; i < resampled; ++i)
        {
            const double at = static_cast<double>(i) * ratio;
            const auto   i0 = static_cast<int>(at);
            const auto   f  = static_cast<float>(at - static_cast<double>(i0));
            const float  y0 = mono[static_cast<std::size_t>(std::min(i0, frames - 1))];
            const float  y1 = mono[static_cast<std::size_t>(std::min(i0 + 1, frames - 1))];
            samples[static_cast<std::size_t>(i)] = y0 + f * (y1 - y0);
        }

        name = file.getFileName().toStdString();
        return true;
    }

    /// A relative path is resolved against the working directory first, then
    /// the shipped examples, so a circuit can name a sample next to itself.
    static juce::File resolve(std::string_view path)
    {
        const juce::String text{std::string(path)};

        if (text.startsWith("~/"))
            return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile(text.substring(2));

        if (juce::File::isAbsolutePath(text))
            return juce::File(text);

        const auto relative = juce::File::getCurrentWorkingDirectory().getChildFile(text);
        if (relative.existsAsFile())
            return relative;

        const auto shipped = juce::File(VALIS_EXAMPLES_DIR).getChildFile(text);
        if (shipped.existsAsFile())
            return shipped;

        const auto root = juce::File(VALIS_ROOT_DIR).getChildFile(text);
        if (root.existsAsFile())
            return root;

        return relative;
    }
};

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
        random.seed(0x5eed1234u);

        for (auto& grain : grains)
            grain.active = false;
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
            spawn(positionCtl, grainLength, pitch, spray, pitchJitter, spread, reverse);

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
                    spawn(positionCtl, grainLength, pitch, spray, pitchJitter, spread, reverse);
                    const float wobble = 1.0f + jitter * random.bipolar() * 0.9f;
                    untilNextGrain += static_cast<double>(std::max(1.0f, interval * wobble));
                }
            }

            scanOffset += scanStep;

            float left = 0.0f, right = 0.0f;

            for (auto& grain : grains)
            {
                if (! grain.active)
                    continue;

                const float window = windowAt(grain.phase, fade);
                const float value  = read(grain.readPos) * window;

                left  += value * grain.panL;
                right += value * grain.panR;

                grain.readPos += grain.increment;
                while (grain.readPos >= static_cast<double>(length)) grain.readPos -= static_cast<double>(length);
                while (grain.readPos < 0.0)                          grain.readPos += static_cast<double>(length);

                grain.phase += grain.phaseInc;
                if (grain.phase >= 1.0f)
                    grain.active = false;
            }

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
    struct Grain
    {
        bool   active    = false;
        double readPos   = 0.0;
        double increment = 1.0;
        float  phase     = 0.0f;
        float  phaseInc  = 0.0f;
        float  panL      = 0.70710678f;
        float  panR      = 0.70710678f;
    };

    /// Grains are a fixed pool. When they are all busy the onset is dropped:
    /// the alternative on this thread is to allocate, and a texture missing one
    /// grain in a cloud of sixty-four is inaudible.
    static constexpr int kMaxGrains = 64;

    void spawn(float position, float grainLength, float pitch, float spray,
               float pitchJitter, float spread, float reverse) noexcept
    {
        if (length <= 0)
            return;

        Grain* slot = nullptr;
        for (auto& grain : grains)
        {
            if (! grain.active)
            {
                slot = &grain;
                break;
            }
        }

        if (slot == nullptr)
            return;

        const double span = static_cast<double>(length);

        // position is measured forward from the write head: with a file loaded
        // the head sits at the start of the file, and while recording the head
        // is the present moment.
        double start = static_cast<double>(writeHead)
                     + static_cast<double>(position) * span
                     + scanOffset
                     + static_cast<double>(spray * random.bipolar()) * span;

        while (start >= span) start -= span;
        while (start < 0.0)   start += span;

        const float semitones = pitch + pitchJitter * random.bipolar();
        double increment = std::pow(2.0, static_cast<double>(semitones) / 12.0);
        if (random.next() < reverse)
            increment = -increment;

        // Equal-power placement around the centre, widened by spread.
        const float pan   = 0.5f + 0.5f * spread * random.bipolar();
        const float angle = pan * 1.5707963267948966f;

        slot->active    = true;
        slot->readPos   = start;
        slot->increment = increment;
        slot->phase     = 0.0f;
        slot->phaseInc  = 1.0f / grainLength;
        slot->panL      = std::cos(angle);
        slot->panR      = std::sin(angle);
    }

    /// Tukey window: `fade` is the fraction of the grain spent rising or
    /// falling. At the smallest fade it is nearly rectangular, keeping the
    /// source transient intact; at 0.5 the whole grain is one rise and fall and
    /// the grain boundary is inaudible.
    ///
    /// The raised cosine is approximated by the cubic 3t^2 - 2t^3, which agrees
    /// with it to within 0.02 and has the same zero slope at both ends. This
    /// runs once per grain per sample, so a cosine call here would dominate the
    /// element's cost.
    static float windowAt(float phase, float fade) noexcept
    {
        float t = 1.0f;

        if (phase < fade)
            t = phase / fade;
        else if (phase > 1.0f - fade)
            t = (1.0f - phase) / fade;
        else
            return 1.0f;

        return t * t * (3.0f - 2.0f * t);
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

        fileContent = std::move(file.samples);
        fileLength  = static_cast<int>(fileContent.size());

        if (fileLength + 4 > static_cast<int>(buffer.size()))
            allocateBuffer(static_cast<double>(fileLength) / sampleRate + 0.1);

        reset();
        return true;
    }

    std::vector<float> buffer;      ///< the circular recording buffer
    std::vector<float> fileContent; ///< what val:file loaded, for reset()

    std::array<Grain, kMaxGrains> grains{};
    Xorshift random;

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
        if (key != "file")
            return true;

        SampleFile file;
        if (! file.load(value, sampleRate, error))
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
            if (trigger > 0.5f && lastTrigger <= 0.5f)
            {
                readPos = static_cast<double>(startCtl) * static_cast<double>(length);
                playing = true;
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

    std::vector<float> samples;
    double sampleRate = 44100.0;
    double readPos    = 0.0;
    bool   playing    = false;
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

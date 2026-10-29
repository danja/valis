// src/dsp/GrainCloud.h
//
// The two halves of a granulator, so they can be one element or two.
//
// GrainMaterial is what grains are cut from: a circular buffer, the point it is
// written at, and how much of it holds anything. GrainCloudCore is the grains: when they
// start, where in the material, how they sound, and the outputs they sum into.
//
// val:Granulator owns one of each. val:GrainBuffer owns a material and offers it on a
// buffer port; val:GrainCloud owns a core and reads whatever material its buffer port was
// given. The grain arithmetic is the same code either way, which is what keeps the single
// element and the pair sounding alike.

#pragma once

#include "dsp/GrainPool.h"
#include "dsp/Random.h"
#include "dsp/Scale.h"
#include "dsp/elements/Common.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace valis::elements {

/// The material grains read. The owner writes it on the audio thread, in its own process(),
/// and a reader runs after it in the same block, so a single thread touches it at a time.
struct GrainMaterial final : SharedBuffer
{
    std::vector<float> buffer;       ///< the circular recording buffer
    std::vector<float> fileContent;  ///< what val:file loaded, for restoring on reset
    double sampleRate    = 44100.0;
    double bufferSeconds = 8.0;
    int    writeHead  = 0;
    int    length     = 0;   ///< how much of the buffer holds material
    int    fileLength = 0;

    /// Whether the owner is writing the buffer this block. A grain that starts while it is
    /// has to stay behind the write head; see GrainSpec::head.
    bool recording = false;

    void allocate(double seconds)
    {
        bufferSeconds = seconds;
        buffer.assign(static_cast<std::size_t>(seconds * sampleRate) + 4, 0.0f);
    }

    /// Back to what the file loaded, or to nothing if no file has been.
    void restore()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);

        // val:seconds may have shrunk the buffer since the file was read, so what is
        // restored is whatever still fits.
        length = std::min(fileLength, static_cast<int>(buffer.size()));
        if (length > 0)
            std::copy(fileContent.begin(), fileContent.begin() + length, buffer.begin());

        writeHead = 0;
        recording = false;
    }

    void write(float sample) noexcept
    {
        const int capacity = static_cast<int>(buffer.size());
        buffer[static_cast<std::size_t>(writeHead)] = sample;
        if (++writeHead >= capacity)
            writeHead = 0;
        if (length < capacity)
            ++length;
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
};

/// Grains: onsets, placement, windows and the stereo outputs. Holds no material.
class GrainCloudCore
{
public:
    static constexpr std::uint32_t kDefaultSeed = 0x5eed1234u;

    void prepare(const ElementType& type, double rate)
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
        triggerIndex     = controlIndex(type, "trigger");
        scanIndex        = controlIndex(type, "scan");
        sizeJitterIndex  = controlIndex(type, "sizeJitter");
        curveIndex       = controlIndex(type, "curve");
        rangeIndex       = controlIndex(type, "range");
        directionIndex   = controlIndex(type, "direction");
        scaleIndex       = controlIndex(type, "scale");
        seedIndex        = controlIndex(type, "seed");

        outIndex   = audioOutIndex(type, "out");
        leftIndex  = audioOutIndex(type, "left");
        rightIndex = audioOutIndex(type, "right");
    }

    void reset()
    {
        untilNextGrain = 0.0;
        scanOffset     = 0.0;
        lastTrigger    = 0.0f;
        lastInterval   = 0.0f;
        lastSeed       = 0;
        grains.seed(kDefaultSeed);
        grains.clear();
    }

    /// One block of grains over `material`. `record(i)` runs at the top of sample `i`,
    /// before that sample's grains are started or mixed, and is where an element that also
    /// writes the material does so; an element that only reads passes a function that does
    /// nothing. `recording` is whether the material is being written this block.
    template <typename Record>
    void run(const ProcessArgs& args, GrainMaterial& material, bool recording, Record&& record) noexcept
    {
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

        if (material.buffer.empty())
        {
            // No material, such as a cloud nothing is connected to: no grains, and the
            // control outputs still say so.
            if (args.numControlOut > 0) args.controlOut[0] = 0.0f;
            if (args.numControlOut > 1) args.controlOut[1] = 0.0f;
            return;
        }

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
        const float  trigger     = controlAt(args, triggerIndex, -1.0f);
        const float  scan        = std::clamp(controlAt(args, scanIndex, 0.0f), -4.0f, 4.0f);

        const float  sizeJitter  = std::clamp(controlAt(args, sizeJitterIndex, 0.0f), 0.0f, 1.0f);
        const float  curve       = std::clamp(controlAt(args, curveIndex, 0.0f), 0.0f, 1.0f);
        const float  range       = std::clamp(controlAt(args, rangeIndex, 0.0f), 0.0f, 24.0f);
        const auto   direction   = static_cast<dsp::Direction>(
            std::clamp(static_cast<int>(controlAt(args, directionIndex, 2.0f) + 0.5f), 0, 2));
        const auto   scale       = static_cast<dsp::ScaleKind>(
            dsp::clampScale(static_cast<int>(controlAt(args, scaleIndex, 1.0f) + 0.5f)));
        const int    seedCtl     = std::clamp(static_cast<int>(controlAt(args, seedIndex, 0.0f) + 0.5f), 0, 65535);

        // A new seed restarts the generator, so the same seed gives the same
        // cloud. 0 is the default sequence.
        if (seedCtl != lastSeed)
        {
            grains.seed(seedCtl != 0 ? static_cast<std::uint32_t>(seedCtl) : kDefaultSeed);
            lastSeed = seedCtl;
        }

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

        // The read point drifts at `scan` buffer lengths per second. It advances
        // per sample rather than per block, so a grain spawned part-way through
        // a block starts where it would have at any other buffer size.
        const double scanStep = static_cast<double>(scan)
                              * static_cast<double>(std::max(material.length, 1)) / sampleRate;

        // One grain per rising edge when an external clock drives the element.
        // The edge is tracked in both modes, so a circuit that switches between
        // them by moving the resting value of the arc behaves the same however
        // it got there.
        int onsets = 0;
        const auto spawn = [&]()
        {
            auto spec = grainSpec(material, positionCtl, grainLength, pitch, spray, pitchJitter, spread, reverse);
            spec.lengthJitter = sizeJitter;
            spec.range        = range;
            spec.direction    = direction;
            spec.scale        = scale;
            if (recording)
                spec.head = static_cast<double>(material.writeHead);
            if (grains.spawn(spec))
                ++onsets;
        };

        if (externalTrigger && trigger > 0.5f && lastTrigger <= 0.5f)
            spawn();

        lastTrigger = trigger;

        for (int i = 0; i < n; ++i)
        {
            record(i);

            // Free-running grain onsets. The countdown lives in a member and is
            // driven by the sample stream, never by the index within this
            // block, so the result does not change with the host's buffer size.
            if (! externalTrigger)
            {
                untilNextGrain -= 1.0;
                while (untilNextGrain <= 0.0)
                {
                    spawn();
                    const float wobble = 1.0f + jitter * grains.generator().bipolar() * 0.9f;
                    untilNextGrain += static_cast<double>(std::max(1.0f, interval * wobble));
                }
            }

            scanOffset += scanStep;

            float left = 0.0f, right = 0.0f;

            grains.mix(left, right, fade, curve, static_cast<double>(material.length),
                       [&material](double position) { return material.read(position); });

            left  *= makeup;
            right *= makeup;

            if (outLeft  != nullptr) outLeft[i]  = left;
            if (outRight != nullptr) outRight[i] = right;
            if (outMono  != nullptr) outMono[i]  = 0.5f * (left + right);
        }

        // Outputs are written every block. onset is a pulse for any block in
        // which a grain started, which is what a control-rate consumer can see.
        if (args.numControlOut > 0) args.controlOut[0] = static_cast<float>(grains.activeCount());
        if (args.numControlOut > 1) args.controlOut[1] = onsets > 0 ? 1.0f : 0.0f;

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
    dsp::GrainSpec grainSpec(const GrainMaterial& material, float position, float grainLength, float pitch,
                        float spray, float pitchJitter, float spread, float reverse) const noexcept
    {
        dsp::GrainSpec spec;
        spec.span          = static_cast<double>(material.length);
        spec.origin        = static_cast<double>(material.writeHead) + scanOffset;
        spec.position      = position;
        spec.lengthSamples = grainLength;
        spec.pitch         = pitch;
        spec.spray         = spray;
        spec.pitchJitter   = pitchJitter;
        spec.spread        = spread;
        spec.reverse       = reverse;
        return spec;
    }

    /// A cloud of sixty-four is more than the ear resolves, and the pool is
    /// shared machinery: see src/dsp/GrainPool.h.
    dsp::GrainPool<64> grains;

    double sampleRate     = 44100.0;
    double untilNextGrain = 0.0;
    double scanOffset     = 0.0;
    float  lastTrigger    = 0.0f;
    float  lastInterval   = 0.0f;
    int    lastSeed       = 0;

    int positionIndex = -1, sizeIndex = -1, densityIndex = -1, pitchIndex = -1;
    int sprayIndex = -1, jitterIndex = -1, pitchJitterIndex = -1, shapeIndex = -1;
    int spreadIndex = -1, reverseIndex = -1, triggerIndex = -1;
    int scanIndex = -1, sizeJitterIndex = -1, curveIndex = -1, rangeIndex = -1;
    int directionIndex = -1, scaleIndex = -1, seedIndex = -1;
    int outIndex = -1, leftIndex = -1, rightIndex = -1;
};

}  // namespace valis::elements

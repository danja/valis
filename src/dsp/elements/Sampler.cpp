// src/dsp/elements/Sampler.cpp
//
// A sampler: notes play recorded sounds. A table maps each key and velocity to a sound (src/dsp/SampleMap.h);
// a note starts a voice that reads its sound at a speed set by the key, through an amplitude envelope. The
// element is polyphonic on its own, with a fixed pool of voices, so a chord or a drum kit needs no voice
// subcircuit.
//
// Files are read and resampled on the message thread, in setOption(). process() only reads them, and
// allocates nothing.

#include "Common.h"

#include "dsp/SampleMap.h"

#include <array>
#include <cmath>
#include <string>

namespace valis::elements {

class Sampler final : public DspElement
{
    static constexpr int kVoices      = 16;
    static constexpr int kMaxCandidates = 32;
    static constexpr double kEndFade  = 32.0;   ///< frames faded out at the end of a sound

    enum class Stage { idle, attack, sustain, release };

    struct Voice
    {
        Stage  stage = Stage::idle;
        int    zone = 0, note = 0;
        double position = 0.0, speed = 1.0;
        float  level = 0.0f;            ///< velocity and zone gain
        float  envelope = 0.0f;
        float  attackStep = 1.0f, releaseStep = 0.0f;
        bool   looping = false, oneShot = false;
        std::uint64_t age = 0;
    };

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate     = rate;
        gainIndex      = controlIndex(type, "gain");
        tuneIndex      = controlIndex(type, "tune");
        attackIndex    = controlIndex(type, "attack");
        releaseIndex   = controlIndex(type, "release");
        loopIndex      = controlIndex(type, "loop");
        keytrackIndex  = controlIndex(type, "keytrack");
        oneShotIndex   = controlIndex(type, "oneshot");
        sensitivityIndex = controlIndex(type, "velocity");
        chokeIndex     = controlIndex(type, "choke");
        reset();
    }

    bool setOption(std::string_view key, std::string_view value, std::string& error) override
    {
        // A failed option leaves what was there, including for the options that arrive after it.
        const auto keptFile = file, keptTable = table;
        const auto keptRoot = rootNote;
        const bool ok = apply(key, value, error);
        if (! ok)
        {
            file = keptFile;
            table = keptTable;
            rootNote = keptRoot;
        }
        return ok;
    }

    void reset() override
    {
        for (auto& v : voices) v = Voice{};
        previous.fill(0.0f);
        roundRobin = 0;
        clock = 0;
        lastChoke = 0.0f;
        gainGlide.reset();
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        float* out = args.audioOut[0];
        const int n = args.numSamples;
        std::fill(out, out + n, 0.0f);

        const float tune     = controlAt(args, tuneIndex, 0.0f);
        const float attackMs = std::max(controlAt(args, attackIndex, 0.0f), 0.0f);
        const float releaseMs = std::max(controlAt(args, releaseIndex, 30.0f), 0.0f);
        const bool  loop     = controlAt(args, loopIndex, 1.0f) > 0.5f;
        const bool  keytrack = controlAt(args, keytrackIndex, 1.0f) > 0.5f;
        const bool  oneShot  = controlAt(args, oneShotIndex, 0.0f) > 0.5f;
        const float sensitivity = std::clamp(controlAt(args, sensitivityIndex, 1.0f), 0.0f, 1.0f);
        const float gainTarget = dbToGain(controlAt(args, gainIndex, 0.0f));
        const float choke    = controlAt(args, chokeIndex, 0.0f);

        if (choke > 0.5f && lastChoke <= 0.5f)
            for (auto& v : voices)
                if (v.stage != Stage::idle)
                    startRelease(v, 2.0f);
        lastChoke = choke;

        if (map.empty())
            return;

        // The notes held now: from the engine's per-note velocities where it gives them, else from the
        // single gate and note number, which is all a bare fixture supplies.
        std::array<float, 128> now {};
        if (args.noteVelocities != nullptr)
            std::copy(args.noteVelocities, args.noteVelocities + 128, now.begin());
        else if (args.gate && args.noteNumber >= 0 && args.noteNumber < 128)
            now[static_cast<std::size_t>(args.noteNumber)] = std::max(args.velocity, 0.01f);

        for (int k = 0; k < 128; ++k)
        {
            const auto i = static_cast<std::size_t>(k);
            if (now[i] > 0.0f && previous[i] <= 0.0f)
                noteOn(k, now[i], tune, keytrack, loop, oneShot, sensitivity, attackMs);
        }
        for (auto& v : voices)
            if ((v.stage == Stage::attack || v.stage == Stage::sustain) && ! v.oneShot
                && now[static_cast<std::size_t>(v.note)] <= 0.0f)
                startRelease(v, releaseMs);
        previous = now;

        // The level glides once per slice, so a turned knob does not step.
        const float gain = gainGlide.step(gainTarget, std::pow(timeToCoeff(10.0f, sampleRate), static_cast<float>(n)));
        for (auto& v : voices)
            if (v.stage != Stage::idle)
                render(v, out, n, gain);
    }

private:
    bool apply(std::string_view key, std::string_view value, std::string& error)
    {
        if (key == "file")
            file = std::string(value);
        else if (key == "rootNote")
        {
            try { rootNote = std::stoi(std::string(value)); }
            catch (...)
            {
                error = "val:rootNote must be a MIDI note number, not '" + std::string(value) + "'";
                return false;
            }
        }
        else if (key == "samples")
            table = std::string(value);
        else
            return true;

        return rebuild(error);
    }

    bool rebuild(std::string& error)
    {
        if (sampleRate <= 0.0)
            return true;

        std::string text;
        if (! file.empty())
        {
            const bool quote = file.find(' ') != std::string::npos;
            text += "file=" + (quote ? "\"" + file + "\"" : file) + " root=" + std::to_string(rootNote) + "\n";
        }
        text += table;

        dsp::SampleMap built;
        if (! built.parse(text, sampleRate, error))
            return false;

        // The audio thread is not running this element while the engine loads it, so the swap is safe.
        map = std::move(built);
        for (auto& v : voices) v = Voice{};
        return true;
    }

    void noteOn(int key, float velocity, float tune, bool keytrack, bool loop, bool oneShot,
                float sensitivity, float attackMs) noexcept
    {
        int found[kMaxCandidates];
        const int count = map.candidates(key, velocity, found, kMaxCandidates);
        if (count <= 0)
            return;

        // Layers that share a key and a velocity take turns, so a repeated hit does not repeat exactly.
        const int zoneIndex = found[roundRobin++ % static_cast<std::uint32_t>(count)];
        const auto& zone = map.zones[static_cast<std::size_t>(zoneIndex)];

        if (zone.chokeGroup != 0)
            for (auto& other : voices)
                if (other.stage != Stage::idle && map.zones[static_cast<std::size_t>(other.zone)].chokeGroup == zone.chokeGroup)
                    startRelease(other, 2.0f);

        Voice* slot = nullptr;
        for (auto& v : voices)
            if (v.stage == Stage::idle) { slot = &v; break; }
        if (slot == nullptr)
        {
            // Nothing free: take a voice already fading, else the oldest. A 1 ms minimum attack keeps the
            // replacement from clicking.
            for (auto& v : voices)
                if (v.stage == Stage::release && (slot == nullptr || v.envelope < slot->envelope)) slot = &v;
            if (slot == nullptr)
                for (auto& v : voices)
                    if (slot == nullptr || v.age < slot->age) slot = &v;
        }

        Voice& v = *slot;
        v = Voice{};
        v.stage = Stage::attack;
        v.zone = zoneIndex;
        v.note = key;
        v.oneShot = oneShot;
        v.looping = loop && ! oneShot;
        v.age = ++clock;

        const double semitones = (keytrack ? static_cast<double>(key - zone.root) : 0.0)
                               + static_cast<double>(zone.tune) + static_cast<double>(tune);
        v.speed = std::pow(2.0, semitones / 12.0);
        v.position = zone.reverse ? zone.end - 1.0 : zone.start;
        if (zone.reverse) v.speed = -v.speed;

        v.level = ((1.0f - sensitivity) + sensitivity * velocity) * dbToGain(zone.gainDb);
        v.attackStep = 1.0f / std::max(attackMs, 1.0f) / static_cast<float>(sampleRate * 0.001);
        v.envelope = 0.0f;
    }

    void startRelease(Voice& v, float milliseconds) noexcept
    {
        if (v.stage == Stage::idle)
            return;
        v.stage = Stage::release;
        v.looping = false;
        v.releaseStep = v.envelope / std::max(milliseconds * static_cast<float>(sampleRate * 0.001), 1.0f);
    }

    /// The sound at a position, by cubic Hermite interpolation. Positions outside it read as silence.
    static float at(const std::vector<float>& s, double position) noexcept
    {
        const int last = static_cast<int>(s.size()) - 1;
        const int i = static_cast<int>(std::floor(position));
        const float f = static_cast<float>(position - static_cast<double>(i));
        const auto get = [&](int k) { return k < 0 || k > last ? 0.0f : s[static_cast<std::size_t>(k)]; };
        const float y0 = get(i - 1), y1 = get(i), y2 = get(i + 1), y3 = get(i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    void render(Voice& v, float* out, int n, float gain) noexcept
    {
        const auto& zone = map.zones[static_cast<std::size_t>(v.zone)];
        const auto& samples = *zone.samples;
        const bool wholeLoop = v.looping && ! zone.hasLoop();
        const double loopStart = zone.hasLoop() ? zone.loopStart : zone.start;
        const double loopEnd   = zone.hasLoop() ? zone.loopEnd : zone.end;
        const double loopLength = loopEnd - loopStart;

        for (int i = 0; i < n; ++i)
        {
            if (v.stage == Stage::attack)
            {
                v.envelope += v.attackStep;
                if (v.envelope >= 1.0f) { v.envelope = 1.0f; v.stage = Stage::sustain; }
            }
            else if (v.stage == Stage::release)
            {
                v.envelope -= v.releaseStep;
                if (v.envelope <= 0.0f) { v = Voice{}; return; }
            }

            const bool wraps = v.looping && (zone.hasLoop() || wholeLoop) && loopLength > 1.0 && v.speed > 0.0;
            if (wraps && v.position >= loopEnd)
                v.position -= loopLength;

            const bool ended = ! wraps && (v.speed > 0.0 ? v.position >= zone.end - 1.0 : v.position <= zone.start);
            if (ended) { v = Voice{}; return; }

            float s = at(samples, v.position);
            if (v.looping && zone.fade > 0.0 && v.speed > 0.0 && v.position >= loopEnd - zone.fade)
            {
                const float w = static_cast<float>((v.position - (loopEnd - zone.fade)) / zone.fade);
                s = s * (1.0f - w) + at(samples, v.position - loopLength) * w;
            }

            const double remaining = v.speed > 0.0 ? zone.end - 1.0 - v.position : v.position - zone.start;
            const float tail = remaining < kEndFade ? static_cast<float>(remaining / kEndFade) : 1.0f;

            out[i] += s * v.level * v.envelope * tail * gain;
            v.position += v.speed;

            // Wrapped here, not at the top of the next sample, so a note released between samples
            // never finds its position already past the end of the loop.
            if (wraps && v.position >= loopEnd)
                v.position -= loopLength;
        }
    }

    double sampleRate = 0.0;
    std::string file, table;
    int rootNote = 60;
    dsp::SampleMap map;

    std::array<Voice, kVoices> voices {};
    std::array<float, 128> previous {};
    std::uint32_t roundRobin = 0;
    std::uint64_t clock = 0;
    float lastChoke = 0.0f;
    Smoothed gainGlide;
    int gainIndex = -1, tuneIndex = -1, attackIndex = -1, releaseIndex = -1, loopIndex = -1;
    int keytrackIndex = -1, oneShotIndex = -1, sensitivityIndex = -1, chokeIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerSampler(ElementRegistry& registry)
{
    registry.add("Sampler", &make<elements::Sampler>);
}

}  // namespace valis

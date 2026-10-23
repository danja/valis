// src/dsp/elements/Wavetable.cpp
//
// A wavetable oscillator that does not alias. Each frame of each set is built from
// its harmonics by additive synthesis, at eleven octave-spaced levels: the first keeps
// every harmonic the table can hold, and each next one has half as many. The player
// picks the level whose highest harmonic is under Nyquist at the note being played,
// so no partial can fold back, however bright the frame.
//
// The tables are the same for every instance, so they are built once, on the message
// thread, the first time an element is prepared, and shared read-only after that.

#include "Common.h"

#include <array>
#include <vector>

namespace valis::elements {

namespace {

constexpr int kTableSize = 2048;
constexpr int kFrames    = 10;     // 1/9 apart, so a shape placed at thirds is exact
constexpr int kLevels    = 11;     // 1024, 512, ... 1 harmonic
constexpr int kSets      = 3;

constexpr double kTwoPi = 6.283185307179586;

constexpr int harmonicsAtLevel(int level) { return (kTableSize / 2) >> level; }

/// Harmonic amplitudes of one frame, indexed by harmonic number. Phases are all sine,
/// so a frame is a sum of sin(k x) terms.
using Spectrum = std::array<double, kTableSize / 2 + 1>;

Spectrum shapeSpectrum(int shape)
{
    Spectrum a{};
    for (int k = 1; k <= kTableSize / 2; ++k)
    {
        const bool odd = (k & 1) != 0;
        switch (shape)
        {
            case 0: a[static_cast<std::size_t>(k)] = k == 1 ? 1.0 : 0.0; break;                   // sine
            case 1: a[static_cast<std::size_t>(k)] =                                               // triangle
                        odd ? ((k & 3) == 1 ? 1.0 : -1.0) / (static_cast<double>(k) * k) : 0.0; break;
            case 2: a[static_cast<std::size_t>(k)] = 1.0 / k; break;                               // saw
            default: a[static_cast<std::size_t>(k)] = odd ? 1.0 / k : 0.0; break;                  // square
        }
    }
    return a;
}

Spectrum frameSpectrum(int set, double t)
{
    Spectrum a{};

    if (set == 0)
    {
        // Sine, triangle, saw, square, a third of the range each, blended linearly.
        const double scaled = t * 3.0;
        const int    from   = std::min(static_cast<int>(scaled), 2);
        const double blend  = scaled - from;
        const Spectrum lo = shapeSpectrum(from);
        const Spectrum hi = shapeSpectrum(from + 1);
        for (std::size_t k = 0; k < a.size(); ++k)
            a[k] = lo[k] * (1.0 - blend) + hi[k] * blend;
    }
    else if (set == 1)
    {
        // A falling spectrum that levels out: a dark, almost pure tone at one end and
        // a buzzy one at the other.
        const double tilt = 2.0 - 1.8 * t;
        for (int k = 1; k <= kTableSize / 2; ++k)
            a[static_cast<std::size_t>(k)] = std::pow(static_cast<double>(k), -tilt);
    }
    else
    {
        // Two formant peaks moving through a, e, i, o, u. Harmonic numbers stand for
        // the formants of a voice at about 110 Hz; they do not follow the note played.
        static constexpr double f1[5] = { 6.6, 4.8, 2.5, 5.2, 2.7 };
        static constexpr double f2[5] = { 9.9, 16.7, 20.8, 7.6, 7.9 };

        const double scaled = t * 4.0;
        const int    from   = std::min(static_cast<int>(scaled), 3);
        const double blend  = scaled - from;
        const double c1 = f1[from] * (1.0 - blend) + f1[from + 1] * blend;
        const double c2 = f2[from] * (1.0 - blend) + f2[from + 1] * blend;

        for (int k = 1; k <= kTableSize / 2; ++k)
        {
            const double x1 = (k - c1) / 1.3;
            const double x2 = (k - c2) / 1.8;
            a[static_cast<std::size_t>(k)] =
                (std::exp(-x1 * x1) + 0.6 * std::exp(-x2 * x2)) / k + 0.01 / (static_cast<double>(k) * k);
        }
    }
    return a;
}

/// Every frame at every level, for every set. [set][frame][level] is one table, with a
/// guard sample after the last so interpolation needs no wrap.
struct Tables
{
    std::vector<float> data;

    Tables()
    {
        data.assign(static_cast<std::size_t>(kSets) * kFrames * kLevels * (kTableSize + 1), 0.0f);

        std::vector<double> sine(kTableSize);
        for (int n = 0; n < kTableSize; ++n)
            sine[static_cast<std::size_t>(n)] = std::sin(kTwoPi * n / kTableSize);

        std::vector<double> accumulator(kTableSize);
        for (int set = 0; set < kSets; ++set)
            for (int frame = 0; frame < kFrames; ++frame)
            {
                const Spectrum spectrum = frameSpectrum(set, static_cast<double>(frame) / (kFrames - 1));

                // From the fewest harmonics up: each level adds the harmonics between
                // its own limit and the limit of the level below it.
                std::fill(accumulator.begin(), accumulator.end(), 0.0);
                int added = 0;

                for (int level = kLevels - 1; level >= 0; --level)
                {
                    for (int k = added + 1; k <= harmonicsAtLevel(level); ++k)
                    {
                        const double amplitude = spectrum[static_cast<std::size_t>(k)];
                        if (std::abs(amplitude) < 1.0e-30)
                            continue;
                        for (int n = 0, index = 0; n < kTableSize; ++n, index = (index + k) & (kTableSize - 1))
                            accumulator[static_cast<std::size_t>(n)] += amplitude * sine[static_cast<std::size_t>(index)];
                    }
                    added = harmonicsAtLevel(level);

                    levelSnapshots[static_cast<std::size_t>(level)] = accumulator;
                }

                // Each table is scaled to peak at 1. Scaling a frame once, at full bandwidth,
                // leaves a pulse-like frame a tenth as loud at an ordinary pitch, where most
                // of its harmonics have been dropped.
                for (int level = 0; level < kLevels; ++level)
                {
                    const auto& source = levelSnapshots[static_cast<std::size_t>(level)];
                    double peak = 1.0e-9;
                    for (double v : source)
                        peak = std::max(peak, std::abs(v));

                    float* table = at(set, frame, level);
                    for (std::size_t n = 0; n < static_cast<std::size_t>(kTableSize); ++n)
                        table[n] = static_cast<float>(source[n] / peak);
                    table[kTableSize] = table[0];
                }
            }
    }

    float* at(int set, int frame, int level) { return data.data() + offset(set, frame, level); }

    const float* at(int set, int frame, int level) const { return data.data() + offset(set, frame, level); }

private:
    static std::size_t offset(int set, int frame, int level)
    {
        const auto index = (static_cast<std::size_t>(set) * kFrames + static_cast<std::size_t>(frame))
                               * kLevels + static_cast<std::size_t>(level);
        return index * (static_cast<std::size_t>(kTableSize) + 1);
    }

    std::array<std::vector<double>, kLevels> levelSnapshots;
};

const Tables& sharedTables()
{
    static const Tables tables;
    return tables;
}

}  // namespace

/// Reads a frame of a set, morphing between frames by `position`, at the pitch asked
/// for, with every harmonic below Nyquist and none above it.
class Wavetable final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate    = rate;
        frequencyIndex = controlIndex(type, "frequency");
        positionIndex  = controlIndex(type, "position");
        setIndex       = controlIndex(type, "set");
        tables         = &sharedTables();   // built here, on the message thread, the first time
        reset();
    }

    void reset() override
    {
        phase = 0.0;
        position = -1.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1 || tables == nullptr)
            return;

        const double frequency = std::clamp(static_cast<double>(controlAt(args, frequencyIndex, 220.0f)),
                                            0.01, sampleRate * 0.45);
        const float target = std::clamp(controlAt(args, positionIndex, 0.0f), 0.0f, 1.0f);
        const int   set    = std::clamp(static_cast<int>(controlAt(args, setIndex, 0.0f) + 0.5f), 0, kSets - 1);

        // The highest harmonic that stays under Nyquist picks the level.
        const int allowed = std::max(1, static_cast<int>(0.5 * sampleRate / frequency));
        int level = 0;
        while (level < kLevels - 1 && harmonicsAtLevel(level) > allowed)
            ++level;

        if (position < 0.0f)
            position = target;
        const float step = (target - position) / static_cast<float>(std::max(args.numSamples, 1));

        const double increment = frequency / sampleRate;
        float* out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            position += step;

            const float scaled = std::clamp(position, 0.0f, 1.0f) * (kFrames - 1);
            const int   frame  = std::min(static_cast<int>(scaled), kFrames - 2);
            const float blend  = scaled - static_cast<float>(frame);

            const double  at    = phase * kTableSize;
            const int     index = static_cast<int>(at);
            const float   frac  = static_cast<float>(at - index);

            const float* a = tables->at(set, frame, level);
            const float* b = tables->at(set, frame + 1, level);
            const float  va = a[index] + frac * (a[index + 1] - a[index]);
            const float  vb = b[index] + frac * (b[index + 1] - b[index]);
            out[i] = va + blend * (vb - va);

            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;
        }
        position = target;
    }

private:
    double sampleRate = 44100.0;
    double phase = 0.0;
    float  position = -1.0f;
    const Tables* tables = nullptr;
    int frequencyIndex = -1, positionIndex = -1, setIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerWavetable(ElementRegistry& registry)
{
    registry.add("Wavetable", &make<elements::Wavetable>);
}

}  // namespace valis

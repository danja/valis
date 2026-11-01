// src/dsp/elements/Vocoder.cpp
//
// A channel vocoder: the modulator and the carrier are each split by the same bank of bandpass filters, the
// level of every modulator band is followed, and each carrier band is scaled by it. The carrier takes on the
// modulator's spectral shape as it changes in time, which is what makes a synthesiser speak.
//
// Coefficients are worked out in prepare() and again in process() only when a band control has moved; nothing
// allocates.

#include "Common.h"

#include <array>
#include <cmath>

namespace valis::elements {

class Vocoder final : public DspElement
{
    static constexpr int kMaxBands = 32;
    static constexpr int kStages   = 2;   ///< two bandpass sections per band, for a steeper skirt

    struct Section
    {
        double b0 = 0.0, a1 = 0.0, a2 = 0.0;     ///< bandpass with b1 = 0 and b2 = -b0
        double z1 = 0.0, z2 = 0.0;

        double tick(double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = z2 - a1 * y;
            z2 = -b0 * x - a2 * y;
            return y;
        }
    };

    struct Band
    {
        std::array<Section, kStages> modulator, carrier;
        float level = 0.0f;
    };

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate   = rate;
        bandsIndex   = controlIndex(type, "bands");
        lowIndex     = controlIndex(type, "low");
        highIndex    = controlIndex(type, "high");
        attackIndex  = controlIndex(type, "attack");
        releaseIndex = controlIndex(type, "release");
        mixIndex     = controlIndex(type, "mix");
        tuned = false;
        reset();
    }

    void reset() override
    {
        for (auto& band : bank)
        {
            for (auto& s : band.modulator) s.z1 = s.z2 = 0.0;
            for (auto& s : band.carrier)   s.z1 = s.z2 = 0.0;
            band.level = 0.0f;
        }
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1 || args.numAudioIn < 1)
            return;
        if (args.numAudioIn < 2)
        {
            std::copy(args.audioIn[0], args.audioIn[0] + args.numSamples, args.audioOut[0]);
            return;
        }

        const int   count = std::clamp(static_cast<int>(controlAt(args, bandsIndex, 16.0f) + 0.5f), 2, kMaxBands);
        const double low  = std::clamp(static_cast<double>(controlAt(args, lowIndex, 100.0f)), 20.0, 0.4 * sampleRate);
        const double high = std::clamp(static_cast<double>(controlAt(args, highIndex, 8000.0f)), low * 1.5, 0.45 * sampleRate);
        if (!tuned || count != tunedBands || std::abs(low - tunedLow) > 1.0e-9 || std::abs(high - tunedHigh) > 1.0e-9)
            tune(count, low, high);

        const float attackMs  = std::max(controlAt(args, attackIndex, 5.0f), 0.1f);
        const float releaseMs = std::max(controlAt(args, releaseIndex, 30.0f), 0.1f);
        const float attack    = 1.0f - std::exp(-1000.0f / (attackMs * static_cast<float>(sampleRate)));
        const float release   = 1.0f - std::exp(-1000.0f / (releaseMs * static_cast<float>(sampleRate)));
        const float mix       = std::clamp(controlAt(args, mixIndex, 1.0f), 0.0f, 1.0f);

        const float* modulator = args.audioIn[0];
        const float* carrier   = args.audioIn[1];
        float*       out       = args.audioOut[0];

        // A rectified sine averages 2 / pi of its peak; scaling by pi / 2 makes a band's level read as its amplitude.
        constexpr float kLevelScale = 1.5707963f;

        for (int i = 0; i < args.numSamples; ++i)
        {
            double wet = 0.0;
            for (int b = 0; b < count; ++b)
            {
                Band& band = bank[static_cast<std::size_t>(b)];
                double m = modulator[i], c = carrier[i];
                for (auto& s : band.modulator) m = s.tick(m);
                for (auto& s : band.carrier)   c = s.tick(c);

                const float rectified = std::abs(static_cast<float>(m));
                band.level += (rectified > band.level ? attack : release) * (rectified - band.level);
                wet += c * band.level * kLevelScale;
            }
            const float shaped = std::abs(wet) < 1.0e-20 ? 0.0f : static_cast<float>(wet);
            out[i] = carrier[i] + mix * (shaped - carrier[i]);
        }

        // Without this the idle filters and followers decay into subnormal numbers.
        for (int b = 0; b < count; ++b)
        {
            Band& band = bank[static_cast<std::size_t>(b)];
            if (band.level < 1.0e-12f) band.level = 0.0f;
            for (auto& s : band.modulator) flush(s);
            for (auto& s : band.carrier)   flush(s);
        }
    }

private:
    static void flush(Section& s) noexcept
    {
        if (std::abs(s.z1) < 1.0e-15) s.z1 = 0.0;
        if (std::abs(s.z2) < 1.0e-15) s.z2 = 0.0;
    }

    /// Centres are spaced evenly in log frequency. Q is set so that neighbouring bands cross about 3 dB down.
    void tune(int count, double low, double high) noexcept
    {
        const double ratio = std::pow(high / low, 1.0 / static_cast<double>(count - 1));
        const double q     = 1.0 / (std::sqrt(ratio) - 1.0 / std::sqrt(ratio));

        for (int b = 0; b < count; ++b)
        {
            const double centre = low * std::pow(ratio, b);
            const double w0     = 2.0 * 3.14159265358979323846 * centre / sampleRate;
            const double alpha  = std::sin(w0) / (2.0 * q);
            const double a0     = 1.0 + alpha;
            Section section;
            section.b0 = alpha / a0;
            section.a1 = -2.0 * std::cos(w0) / a0;
            section.a2 = (1.0 - alpha) / a0;

            Band& band = bank[static_cast<std::size_t>(b)];
            for (int s = 0; s < kStages; ++s)
            {
                band.modulator[static_cast<std::size_t>(s)].b0 = section.b0;
                band.modulator[static_cast<std::size_t>(s)].a1 = section.a1;
                band.modulator[static_cast<std::size_t>(s)].a2 = section.a2;
                band.carrier[static_cast<std::size_t>(s)].b0 = section.b0;
                band.carrier[static_cast<std::size_t>(s)].a1 = section.a1;
                band.carrier[static_cast<std::size_t>(s)].a2 = section.a2;
            }
        }
        tuned = true;
        tunedBands = count;
        tunedLow = low;
        tunedHigh = high;
    }

    double sampleRate = 48000.0;
    std::array<Band, kMaxBands> bank {};
    bool   tuned = false;
    int    tunedBands = 0;
    double tunedLow = 0.0, tunedHigh = 0.0;
    int bandsIndex = -1, lowIndex = -1, highIndex = -1, attackIndex = -1, releaseIndex = -1, mixIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerVocoder(ElementRegistry& registry)
{
    registry.add("Vocoder", &make<elements::Vocoder>);
}

}  // namespace valis

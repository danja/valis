// src/dsp/elements/PitchTracker.cpp
//
// val:PitchTracker listens to an audio signal and puts its pitch on control outputs, so an
// oscillator, a filter or a granulator can follow a played note.
//
// It is the YIN estimator of de Cheveigne and Kawahara, "YIN, a fundamental frequency estimator
// for speech and music" (JASA, 2002): the signal is compared with a copy of itself delayed by a
// trial period, the squared difference is normalised by its running mean so that a short period
// does not win only because it is short, and the first period whose normalised difference falls
// below a threshold is the pitch. Taking the first dip and not the deepest keeps it from
// answering an octave low, which a plain autocorrelation does on a sound with strong harmonics.
// https://en.wikipedia.org/wiki/Pitch_detection_algorithm
//
// The estimate is made on a copy of the signal low-passed and reduced to about 12 kHz, since
// the fundamental of a played note is well below that and the cost of the comparison grows with
// the square of the rate. The estimate is refreshed every 5 ms of signal, counted in samples,
// so it does not depend on how the host cuts the stream into blocks. The outputs are written
// once per control slice and hold the last voiced pitch while the signal has none.

#include "Common.h"

#include <array>
#include <cmath>
#include <vector>

namespace valis::elements {

class PitchTracker final : public DspElement
{
    static constexpr double kTargetRate = 12000.0;
    static constexpr double kLowestHz   = 40.0;     // sets the longest period compared
    static constexpr double kWindowSec  = 0.045;    // signal compared with its delayed copy
    static constexpr double kHopSec     = 0.005;

public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        lowIndex       = controlIndex(type, "low");
        highIndex      = controlIndex(type, "high");
        thresholdIndex = controlIndex(type, "threshold");
        floorIndex     = controlIndex(type, "floor");

        factor = std::max(1, static_cast<int>(std::lround(rate / kTargetRate)));
        reducedRate = rate / factor;

        window  = static_cast<int>(std::lround(kWindowSec * reducedRate));
        maxLag  = static_cast<int>(std::lround(reducedRate / kLowestHz));
        hop     = std::max(1, static_cast<int>(std::lround(kHopSec * reducedRate)));
        span    = window + maxLag + 2;

        // Windowed-sinc low-pass at 90% of the reduced rate's Nyquist, unity gain at DC.
        const int taps = factor == 1 ? 1 : 8 * factor + 1;
        filter.assign(static_cast<std::size_t>(taps), 0.0f);
        if (taps == 1)
            filter[0] = 1.0f;
        else
        {
            const double cutoff = 0.45 / factor;
            const double centre = 0.5 * (taps - 1);
            double sum = 0.0;
            for (int k = 0; k < taps; ++k)
            {
                const double t = k - centre;
                const double x = 2.0 * cutoff * t;
                const double sinc = std::abs(x) < 1.0e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * k / (taps - 1));
                filter[static_cast<std::size_t>(k)] = static_cast<float>(sinc * hann);
                sum += filter[static_cast<std::size_t>(k)];
            }
            for (auto& c : filter)
                c = static_cast<float>(c / sum);
        }

        input.assign(static_cast<std::size_t>(taps) * 2, 0.0f);
        ring.assign(static_cast<std::size_t>(span), 0.0f);
        linear.assign(static_cast<std::size_t>(span), 0.0f);
        difference.assign(static_cast<std::size_t>(maxLag + 2), 0.0f);
        normalised.assign(static_cast<std::size_t>(maxLag + 2), 0.0f);

        reset();
    }

    void reset() override
    {
        std::fill(input.begin(), input.end(), 0.0f);
        std::fill(ring.begin(), ring.end(), 0.0f);
        inputPos = 0;
        ringPos  = 0;
        phase    = 0;
        sinceHop = 0;
        filled   = 0;
        frequency = 0.0f;
        clarity   = 0.0f;
        voiced    = false;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1)
            return;

        const float low       = std::clamp(controlAt(args, lowIndex, 50.0f), static_cast<float>(kLowestHz), 2000.0f);
        const float high      = std::clamp(controlAt(args, highIndex, 1500.0f), low, 4000.0f);
        const float threshold = std::clamp(controlAt(args, thresholdIndex, 0.15f), 0.02f, 0.6f);
        const float floorDb   = std::clamp(controlAt(args, floorIndex, -60.0f), -120.0f, 0.0f);

        const int taps = static_cast<int>(filter.size());
        const float* in = args.audioIn[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            // The input history is stored twice so the filter reads one run of memory.
            input[static_cast<std::size_t>(inputPos)] = in[i];
            input[static_cast<std::size_t>(inputPos + taps)] = in[i];
            inputPos = inputPos + 1 == taps ? 0 : inputPos + 1;

            if (++phase < factor)
                continue;
            phase = 0;

            const float* recent = input.data() + inputPos;   // oldest first
            float y = 0.0f;
            for (int k = 0; k < taps; ++k)
                y += filter[static_cast<std::size_t>(k)] * recent[k];

            ring[static_cast<std::size_t>(ringPos)] = y;
            ringPos = ringPos + 1 == span ? 0 : ringPos + 1;
            filled  = std::min(filled + 1, span);

            if (++sinceHop >= hop && filled >= span)
            {
                sinceHop = 0;
                estimate(low, high, threshold, floorDb);
            }
        }

        if (args.numControlOut > 0) args.controlOut[0] = frequency;
        if (args.numControlOut > 1)
            args.controlOut[1] = frequency > 0.0f
                ? 69.0f + 12.0f * std::log2(frequency / 440.0f) : 0.0f;
        if (args.numControlOut > 2) args.controlOut[2] = voiced ? 1.0f : 0.0f;
        if (args.numControlOut > 3) args.controlOut[3] = clarity;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    void estimate(float low, float high, float threshold, float floorDb) noexcept
    {
        // The ring, oldest first.
        for (int i = 0; i < span; ++i)
        {
            int at = ringPos + i;
            if (at >= span) at -= span;
            linear[static_cast<std::size_t>(i)] = ring[static_cast<std::size_t>(at)];
        }

        const float* x = linear.data();

        double energy = 0.0;
        for (int j = 0; j < window; ++j)
            energy += static_cast<double>(x[j]) * x[j];
        const double rms = std::sqrt(energy / window);
        if (rms < std::pow(10.0, static_cast<double>(floorDb) / 20.0))
        {
            voiced  = false;
            clarity = 0.0f;
            return;
        }

        const int minLag = std::max(2, static_cast<int>(reducedRate / high));
        const int topLag = std::min(maxLag, static_cast<int>(reducedRate / low));

        // The squared difference at each trial period, and its running-mean normalisation.
        double running = 0.0;
        normalised[0] = 1.0f;
        for (int tau = 1; tau <= topLag + 1 && tau <= maxLag; ++tau)
        {
            double d = 0.0;
            for (int j = 0; j < window; ++j)
            {
                const double delta = static_cast<double>(x[j]) - x[j + tau];
                d += delta * delta;
            }
            difference[static_cast<std::size_t>(tau)] = static_cast<float>(d);
            running += d;
            normalised[static_cast<std::size_t>(tau)] =
                running > 0.0 ? static_cast<float>(d * tau / running) : 1.0f;
        }

        // The first period below the threshold, followed down to the bottom of its dip.
        int best = -1;
        for (int tau = minLag; tau <= topLag; ++tau)
        {
            if (normalised[static_cast<std::size_t>(tau)] < threshold)
            {
                while (tau + 1 <= topLag
                       && normalised[static_cast<std::size_t>(tau + 1)] < normalised[static_cast<std::size_t>(tau)])
                    ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0)
        {
            // No period was clear. Report how clear the nearest was and keep the last pitch.
            float smallest = 1.0f;
            for (int tau = minLag; tau <= topLag; ++tau)
                smallest = std::min(smallest, normalised[static_cast<std::size_t>(tau)]);
            voiced  = false;
            clarity = std::clamp(1.0f - smallest, 0.0f, 1.0f);
            return;
        }

        // The dip of a periodic signal's difference is a cosine of the lag, 1 - cos(theta * (lag - period)),
        // with theta = 2 pi / period. The three samples round the dip fix that cosine exactly, which a parabola
        // does not: at 12 kHz a period of nine samples would come out several cents sharp.
        double lag = best;
        if (best > minLag && best <= topLag)
        {
            const double a = difference[static_cast<std::size_t>(best - 1)];
            const double b = difference[static_cast<std::size_t>(best)];
            const double c = difference[static_cast<std::size_t>(best + 1)];
            const double theta = 2.0 * kPi / best;
            const double u = (a + c - 2.0 * b) / (2.0 * (1.0 - std::cos(theta)));
            const double v = (a - c) / (2.0 * std::sin(theta));
            if (u > 1.0e-12)
                lag += std::clamp(std::atan2(v, u) / theta, -1.0, 1.0);
        }

        frequency = static_cast<float>(reducedRate / lag);
        clarity   = std::clamp(1.0f - normalised[static_cast<std::size_t>(best)], 0.0f, 1.0f);
        voiced    = true;
    }

    double sampleRate = 48000.0, reducedRate = 12000.0;
    int factor = 4, window = 0, maxLag = 0, hop = 0, span = 0;

    std::vector<float> filter, input, ring, linear, difference, normalised;
    int inputPos = 0, ringPos = 0, phase = 0, sinceHop = 0, filled = 0;

    float frequency = 0.0f, clarity = 0.0f;
    bool  voiced = false;
    int lowIndex = -1, highIndex = -1, thresholdIndex = -1, floorIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerPitchTracker(ElementRegistry& registry)
{
    registry.add("PitchTracker", &make<elements::PitchTracker>);
}

}  // namespace valis

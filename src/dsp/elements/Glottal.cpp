// src/dsp/elements/Glottal.cpp
//
// val:Glottal, a voltage-controlled oscillator whose waveforms are models of the sound the vocal folds make
// (https://en.wikipedia.org/wiki/Source%E2%80%93filter_model), plus a saw and a pulse for comparison. A voice
// synthesiser feeds a pulse like this to formant filters; the shape of the pulse decides whether the voice is
// breathy, normal or pressed, which a saw cannot say.
//
// Shapes, with p the phase (0 to 1), Oq the open quotient (the part of the cycle the folds are apart) and SQ the
// speed quotient (rise time over fall time of the flow):
//   0 Rosenberg   the flow through the glottis, a rising half cosine then a falling quarter cosine
//                 (Rosenberg, 1971). Smooth, so it is dark.
//   1 Klatt       the flow as a cubic, 27/4 x^2 (1 - x) with x = p / Oq (the KLGLOTT88 model, Klatt and Klatt, 1990).
//   2 LF          the derivative of the flow in the Liljencrants-Fant model (Fant, Liljencrants and Lin, 1985), set
//                 by one number, Rd, using the regressions of Fant (1995). The sharp negative peak at closure is
//                 the excitation of the voice; a larger Rd is softer and breathier.
//   3 Radiated    the Rosenberg flow differentiated, which is what leaves the lips (they radiate the derivative).
//   4 Cosine      a raised cosine over Oq: no closed phase to speak of, the breathiest.
//   5 Saw         band-limited with polyBLEP (https://www.martin-finke.de/articles/audio-plugins-018-polyblep-oscillator/).
//   6 Pulse       a band-limited rectangle of width Oq.
//
// Each cycle may be perturbed: jitter changes its length and shimmer its height, by a random amount each cycle
// (the same sequence every render). Breath adds aspiration noise that is louder while the folds are open.
// Tilt darkens the result with a one-pole low-pass. A DC blocker removes the offset a flow has.
//
// The closure of the Rosenberg-derived, Klatt and LF shapes is a corner, and the pulse and saw have steps that are
// corrected; a corner still aliases at high pitch, so use val:oversampling on a note above about 600 Hz.

#include "Common.h"

#include "dsp/Random.h"

#include <cmath>

namespace valis::elements {

class Glottal final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate = rate;
        freqIndex = controlIndex(type, "frequency");   shapeIndex = controlIndex(type, "shape");
        openIndex = controlIndex(type, "open");        skewIndex = controlIndex(type, "skew");
        rdIndex = controlIndex(type, "rd");            breathIndex = controlIndex(type, "breath");
        jitterIndex = controlIndex(type, "jitter");    shimmerIndex = controlIndex(type, "shimmer");
        tiltIndex = controlIndex(type, "tilt");
        blocker = 1.0f - static_cast<float>(6.283185307179586 * 12.0 / rate);
        reset();
    }

    void reset() override
    {
        phase = 0.0;
        random.seed(0x2545f491u);
        cycleStretch = 1.0f;
        cycleGain = 1.0f;
        lowpass = 0.0f;
        dcIn = dcOut = 0.0f;
        cachedRd = -1.0f;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioOut < 1)
            return;

        const float frequency = std::clamp(controlAt(args, freqIndex, 220.0f), 0.01f, static_cast<float>(sampleRate * 0.45));
        const int   shape     = std::clamp(static_cast<int>(controlAt(args, shapeIndex, 0.0f) + 0.5f), 0, 6);
        const float open      = std::clamp(controlAt(args, openIndex, 0.6f), 0.05f, 1.0f);
        const float skew      = std::clamp(controlAt(args, skewIndex, 2.0f), 0.5f, 6.0f);
        const float rd        = std::clamp(controlAt(args, rdIndex, 1.0f), 0.3f, 2.7f);
        const float breath    = std::clamp(controlAt(args, breathIndex, 0.0f), 0.0f, 1.0f);
        const float jitter    = std::clamp(controlAt(args, jitterIndex, 0.0f), 0.0f, 0.2f);
        const float shimmer   = std::clamp(controlAt(args, shimmerIndex, 0.0f), 0.0f, 1.0f);
        const float tilt      = std::clamp(controlAt(args, tiltIndex, 0.0f), 0.0f, 1.0f);

        if (shape == 2 && rd != cachedRd)
            solveLf(rd);

        // Rosenberg's two parts, as fractions of the cycle.
        const float rise = open * skew / (1.0f + skew), fall = open / (1.0f + skew);
        const float tiltCoefficient = tilt <= 0.0f ? 0.0f
            : std::exp(-6.2831853f * 20000.0f * std::pow(0.02f, tilt) / static_cast<float>(sampleRate));

        const double increment = frequency / sampleRate;
        float* out = args.audioOut[0];
        for (int i = 0; i < args.numSamples; ++i)
        {
            const auto p = static_cast<float>(phase);
            const auto dt = static_cast<float>(increment * cycleStretch);
            float y = 0.0f, scale = 1.0f;

            switch (shape)
            {
                case 0:     // Rosenberg flow
                    y = rosenberg(p, rise, fall);
                    scale = 1.2f;
                    break;
                case 1:     // Klatt flow
                    y = p < open ? klatt(p / open) : 0.0f;
                    scale = 1.2f;
                    break;
                case 2:     // Liljencrants-Fant flow derivative
                    y = p < lfTe ? lfE0 * std::exp(lfAlpha * p) * std::sin(lfOmega * p)
                                 : -(1.0f / (lfEps * lfTa)) * (std::exp(-lfEps * (p - lfTe)) - lfTail);
                    scale = 1.0f;
                    break;
                case 3:     // Rosenberg flow derivative
                    y = radiated(p, rise, fall);
                    scale = 1.0f;
                    break;
                case 4:     // raised cosine
                    y = p < open ? 0.5f * (1.0f - std::cos(6.2831853f * p / open)) : 0.0f;
                    scale = 1.2f;
                    break;
                case 5:     // saw
                    y = 2.0f * p - 1.0f - polyBlep(p, dt);
                    break;
                default:    // pulse of width `open`
                {
                    y = p < open ? 1.0f : -1.0f;
                    y += polyBlep(p, dt);
                    float shifted = p - open;
                    if (shifted < 0.0f) shifted += 1.0f;
                    y -= polyBlep(shifted, dt);
                    break;
                }
            }

            y *= scale * cycleGain;

            // Aspiration: noise that follows the opening of the folds.
            if (breath > 0.0f)
                y += breath * 0.5f * random.bipolar() * (0.25f + std::fabs(y));

            if (tiltCoefficient > 0.0f)
            {
                lowpass = tiltCoefficient * lowpass + (1.0f - tiltCoefficient) * y;
                y = lowpass;
            }

            // DC blocker.
            const float blocked = y - dcIn + blocker * dcOut;
            dcIn = y;
            dcOut = blocked;
            out[i] = blocked;

            phase += increment * cycleStretch;
            if (phase >= 1.0)
            {
                phase -= std::floor(phase);
                cycleStretch = 1.0f + jitter * random.bipolar();
                cycleGain = 1.0f + shimmer * 0.5f * random.bipolar();
            }
        }
    }

private:
    static float rosenbergFlow(float p, float rise, float fall) noexcept
    {
        if (p < rise) return 0.5f * (1.0f - std::cos(3.14159265f * p / rise));
        if (p < rise + fall) return std::cos(1.5707963f * (p - rise) / fall);
        return 0.0f;
    }
    static float rosenberg(float p, float rise, float fall) noexcept { return rosenbergFlow(p, rise, fall); }

    /// The derivative of the Rosenberg flow, scaled so its largest value is 1.
    static float radiated(float p, float rise, float fall) noexcept
    {
        const float peak = 1.5707963f / std::min(rise, fall);
        if (p < rise) return 0.5f * 3.14159265f / rise * std::sin(3.14159265f * p / rise) / peak;
        if (p < rise + fall) return -1.5707963f / fall * std::sin(1.5707963f * (p - rise) / fall) / peak;
        return 0.0f;
    }

    static float klatt(float x) noexcept { return 6.75f * x * x * (1.0f - x); }

    /// The correction to subtract around a step at 0 (and by wrapping at 1), as val:Oscillator does.
    static float polyBlep(float t, float dt) noexcept
    {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    /// The Liljencrants-Fant pulse for one Rd, in a cycle of length 1, by Fant's regressions. The growth rate alpha is
    /// found by bisection from the condition that the flow returns to zero over the cycle; epsilon by Newton's method
    /// from the exponential return. Cheap enough to redo when Rd moves, and does not allocate.
    void solveLf(float rdIn) noexcept
    {
        cachedRd = rdIn;
        const double rd = rdIn;
        const double ra = (-1.0 + 4.8 * rd) / 100.0, rk = (22.4 + 11.8 * rd) / 100.0;
        const double rg = rk / (4.0 * ((0.11 * rd) / (0.5 + 1.2 * rk) - ra));
        const double tp = 1.0 / (2.0 * rg), te = tp * (rk + 1.0), ta = ra;
        const double omega = 3.14159265358979 / tp;

        double eps = 1.0 / ta;
        for (int k = 0; k < 12; ++k)
        {
            const double e = std::exp(-eps * (1.0 - te));
            eps -= (eps * ta - 1.0 + e) / (ta - (1.0 - te) * e);
        }
        const double tail = std::exp(-eps * (1.0 - te));

        // Net flow over the cycle for a growth rate a, with E(te) fixed at -1.
        const auto net = [&](double a)
        {
            const double e0 = -1.0 / (std::exp(a * te) * std::sin(omega * te));
            const double first = e0 * (std::exp(a * te) * (a * std::sin(omega * te) - omega * std::cos(omega * te)) + omega)
                                 / (a * a + omega * omega);
            const double second = -(1.0 / (eps * ta)) * ((1.0 - tail) / eps - (1.0 - te) * tail);
            return first + second;
        };

        double lo = -30.0, hi = 300.0;
        const double flo = net(lo);
        for (int k = 0; k < 60; ++k)
        {
            const double mid = 0.5 * (lo + hi);
            if (net(mid) * flo > 0.0) lo = mid; else hi = mid;
        }
        const double alpha = 0.5 * (lo + hi);

        lfTe = static_cast<float>(te);       lfTa = static_cast<float>(ta);
        lfOmega = static_cast<float>(omega); lfAlpha = static_cast<float>(alpha);
        lfEps = static_cast<float>(eps);     lfTail = static_cast<float>(tail);
        lfE0 = static_cast<float>(-1.0 / (std::exp(alpha * te) * std::sin(omega * te)));
    }

    double sampleRate = 44100.0, phase = 0.0;
    float blocker = 0.999f, lowpass = 0.0f, dcIn = 0.0f, dcOut = 0.0f;
    float cycleStretch = 1.0f, cycleGain = 1.0f;
    float cachedRd = -1.0f, lfTe = 0.5f, lfTa = 0.05f, lfOmega = 6.0f, lfAlpha = 3.0f, lfEps = 20.0f, lfTail = 0.0f, lfE0 = 1.0f;
    dsp::Random random;
    int freqIndex = -1, shapeIndex = -1, openIndex = -1, skewIndex = -1, rdIndex = -1, breathIndex = -1,
        jitterIndex = -1, shimmerIndex = -1, tiltIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerGlottal(ElementRegistry& registry)
{
    registry.add("Glottal", &make<elements::Glottal>);
}

}  // namespace valis

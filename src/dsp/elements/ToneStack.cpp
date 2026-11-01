// src/dsp/elements/ToneStack.cpp
//
// The passive treble, middle and bass network of a Fender '59 Bassman. The continuous-time transfer function is in
// ToneStackCoefficients.h; here it is turned into a digital filter by the bilinear transform with c = 2 * rate, as
// Yeh and Smith do, and run in double precision: at a high rate the poles sit very close to z = 1 and a float
// filter loses them.
//
// The three controls are glided and the coefficients are worked out every few samples, counted across blocks, so
// a turned knob does not click and the sound does not depend on how the host cut the stream.

#include "Common.h"
#include "ToneStackCoefficients.h"

#include <cmath>

namespace valis::elements {

class ToneStack final : public DspElement
{
public:
    void prepare(const ElementType& type, double rate, int) override
    {
        sampleRate   = rate;
        trebleIndex  = controlIndex(type, "treble");
        middleIndex  = controlIndex(type, "middle");
        bassIndex    = controlIndex(type, "bass");
        glide        = timeToCoeff(10.0f, rate);
        reset();
    }

    void reset() override
    {
        z1 = z2 = z3 = 0.0;
        trebleGlide.reset();
        middleGlide.reset();
        bassGlide.reset();
        untilUpdate = 0;
    }

    void process(const ProcessArgs& args) noexcept override
    {
        if (args.numAudioIn < 1 || args.numAudioOut < 1)
            return;

        const float treble = std::clamp(controlAt(args, trebleIndex, 0.5f), 0.0f, 1.0f);
        const float middle = std::clamp(controlAt(args, middleIndex, 0.5f), 0.0f, 1.0f);
        const float bass   = std::clamp(controlAt(args, bassIndex,   0.5f), 0.0f, 1.0f);

        const float* in  = args.audioIn[0];
        float*       out = args.audioOut[0];

        for (int i = 0; i < args.numSamples; ++i)
        {
            const float t = trebleGlide.step(treble, glide);
            const float m = middleGlide.step(middle, glide);
            const float l = bassGlide.step(bass, glide);
            if (untilUpdate <= 0)
            {
                design(t, m, l);
                untilUpdate = kUpdate;
            }
            --untilUpdate;

            // Transposed direct form II.
            const double x = in[i];
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y + z3;
            z3 = b3 * x - a3 * y;
            out[i] = static_cast<float>(y);
        }

        // Idle poles near one decay slowly into subnormal numbers.
        if (std::abs(z1) < 1.0e-30) z1 = 0.0;
        if (std::abs(z2) < 1.0e-30) z2 = 0.0;
        if (std::abs(z3) < 1.0e-30) z3 = 0.0;
    }

private:
    static constexpr int kUpdate = 8;

    void design(double t, double m, double bass) noexcept
    {
        const auto h = tonestack::analog(tonestack::Components{}, t, m, tonestack::bassTaper(bass));
        const double c = 2.0 * sampleRate;
        const double c2 = c * c, c3 = c2 * c;

        const double B0 = -h.b[1] * c - h.b[2] * c2 - h.b[3] * c3;
        const double B1 = -h.b[1] * c + h.b[2] * c2 + 3.0 * h.b[3] * c3;
        const double B2 =  h.b[1] * c + h.b[2] * c2 - 3.0 * h.b[3] * c3;
        const double B3 =  h.b[1] * c - h.b[2] * c2 + h.b[3] * c3;
        const double A0 = -h.a[0] - h.a[1] * c - h.a[2] * c2 - h.a[3] * c3;
        const double A1 = -3.0 * h.a[0] - h.a[1] * c + h.a[2] * c2 + 3.0 * h.a[3] * c3;
        const double A2 = -3.0 * h.a[0] + h.a[1] * c + h.a[2] * c2 - 3.0 * h.a[3] * c3;
        const double A3 = -h.a[0] + h.a[1] * c - h.a[2] * c2 + h.a[3] * c3;

        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; b3 = B3 / A0;
        a1 = A1 / A0; a2 = A2 / A0; a3 = A3 / A0;
    }

    double sampleRate = 48000.0;
    double b0 = 0.0, b1 = 0.0, b2 = 0.0, b3 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double z1 = 0.0, z2 = 0.0, z3 = 0.0;
    float  glide = 0.0f;
    Smoothed trebleGlide, middleGlide, bassGlide;
    int untilUpdate = 0;
    int trebleIndex = -1, middleIndex = -1, bassIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerToneStack(ElementRegistry& registry)
{
    registry.add("ToneStack", &make<elements::ToneStack>);
}

}  // namespace valis

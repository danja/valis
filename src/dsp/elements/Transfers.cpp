// src/dsp/elements/Transfers.cpp
//
// Memoryless nonlinearities. Each declares an anti-aliasing strategy in the
// ontology; the shape's antiderivatives make ADAA possible without oversampling.

#include "Common.h"
#include "../Adaa.h"

#include "valis/Vocabulary.h"

namespace valis::elements {

/// Wraps a shape with the anti-aliasing strategy its ontology entry asks for.
/// A strategy the shape cannot support degrades to one it can, and says so
/// through effectiveStrategy() rather than pretending.
template <typename Shape>
class ShapedTransfer : public MonoElement
{
public:
    /// Resolved once in prepare(). Comparing IRIs in process() would build a
    /// std::string per block, which the allocation test rightly rejects.
    enum class Strategy { none, adaa1, adaa2 };

    void reset() override
    {
        adaa1.reset();
        adaa2.reset();
    }

    /// What is actually running, which may be less than what was requested.
    Strategy effectiveStrategy() const { return strategy; }

    /// ADAA evaluates the shape across the interval between samples, so it
    /// delays: measurably one sample for second order, half a sample for first.
    /// Half cannot be reported, so first order declares none and is left as a
    /// sub-sample phase error rather than a rounded-up sample of latency.
    int latencyInSamples() const override
    {
        return strategy == Strategy::adaa2 ? 1 : 0;
    }

protected:
    void cacheIndices(const ElementType& type) override
    {
        gainIndex = controlIndex(type, "gain");
        if (gainIndex < 0)
            gainIndex = controlIndex(type, "threshold");

        // The class supplies the default; the instance may override it.
        strategy = parseStrategy(type.antialiasing);
    }

public:
    void setOption(std::string_view key, std::string_view value) override
    {
        if (key == "antialiasing")
            strategy = parseStrategy(std::string(value));
    }

private:
    static Strategy parseStrategy(const std::string& iri)
    {
        if (iri == vocab::valTerm("ADAA2")) return Strategy::adaa2;
        if (iri == vocab::valTerm("ADAA1")) return Strategy::adaa1;
        return Strategy::none;
    }

protected:

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const float gain = std::max(controlAt(args, gainIndex, 1.0f), 1.0e-4f);

        if (strategy == Strategy::adaa2)
        {
            for (int i = 0; i < n; ++i)
                out[i] = static_cast<float>(adaa2.process(static_cast<double>(in[i]) * gain));
        }
        else if (strategy == Strategy::adaa1)
        {
            for (int i = 0; i < n; ++i)
                out[i] = static_cast<float>(adaa1.process(static_cast<double>(in[i]) * gain));
        }
        else
        {
            for (int i = 0; i < n; ++i)
                out[i] = static_cast<float>(Shape::evaluate(static_cast<double>(in[i]) * gain));
        }
    }

private:
    dsp::Adaa1<Shape> adaa1;
    dsp::Adaa2<Shape> adaa2;
    Strategy strategy = Strategy::none;
    int gainIndex = -1;
};

using Tanh      = ShapedTransfer<dsp::TanhShape>;
using HardClip  = ShapedTransfer<dsp::HardClipShape>;
using SinArcTan = ShapedTransfer<dsp::SinArcTanShape>;
using SoftSine  = ShapedTransfer<dsp::SoftSineShape>;

/// Wavefolder. Reflects the signal back on itself each time it passes +/-1.
class Fold final : public MonoElement
{
protected:
    void cacheIndices(const ElementType& type) override { gainIndex = controlIndex(type, "gain"); }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const float gain = std::max(controlAt(args, gainIndex, 1.0f), 1.0e-4f);

        for (int i = 0; i < n; ++i)
        {
            float x = in[i] * gain;

            // Triangle fold: bounded however hard it is driven.
            x = x - 4.0f * std::floor(0.25f * x + 0.25f);
            x = std::abs(x) - 1.0f;
            out[i] = 1.0f - std::abs(x);
        }
    }

private:
    int gainIndex = -1;
};

/// A single junction diode shunting to ground through a series resistance -
/// the classic asymmetric clipper.
///
/// Conducting, the divider gives (v_in - v_out)/Rs = Is*(exp(v_out/(n*Vt)) - 1),
/// whose solution is well approximated by v_out = n*Vt*log(1 + v_in/(Is*Rs)).
/// Reverse biased the diode is effectively open, so the signal passes intact.
/// That asymmetry is the whole point: a diode is not a tanh with a label.
class Diode final : public MonoElement
{
protected:
    void cacheIndices(const ElementType& type) override
    {
        isIndex = controlIndex(type, "saturationCurrent");
        nIndex  = controlIndex(type, "emissionCoefficient");
        vtIndex = controlIndex(type, "thermalVoltage");
    }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const double is = std::max(static_cast<double>(controlAt(args, isIndex, 2.52e-9f)), 1e-15);
        const double nf = std::max(static_cast<double>(controlAt(args, nIndex, 1.752f)), 1.0);
        const double vt = std::max(static_cast<double>(controlAt(args, vtIndex, 0.02585f)), 1e-4);

        const double vtn   = nf * vt;
        const double scale = 1.0 / (is * kSeriesResistance);

        for (int i = 0; i < n; ++i)
        {
            const double v = in[i];
            out[i] = v > 0.0 ? static_cast<float>(vtn * std::log1p(v * scale))
                             : static_cast<float>(v);
        }
    }

private:
    // The resistance the diode shunts against. Fixed here; DiodePair exposes it.
    static constexpr double kSeriesResistance = 1000.0;

    int isIndex = -1, nIndex = -1, vtIndex = -1;
};

/// Two diodes in antiparallel across the same divider. Their combined current
/// is i = 2*Is*sinh(v/(n*Vt)), so the divider inverts to
///     v_out = n*Vt*asinh(v_in / (2*Is*Rs))
/// which is symmetric - the difference from a single diode, and audible.
class DiodePair final : public MonoElement
{
protected:
    void cacheIndices(const ElementType& type) override
    {
        isIndex = controlIndex(type, "saturationCurrent");
        nIndex  = controlIndex(type, "emissionCoefficient");
        rsIndex = controlIndex(type, "seriesResistance");
    }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const double is = std::max(static_cast<double>(controlAt(args, isIndex, 2.52e-9f)), 1e-15);
        const double nf = std::max(static_cast<double>(controlAt(args, nIndex, 1.752f)), 1.0);
        const double rs = std::max(static_cast<double>(controlAt(args, rsIndex, 1000.0f)), 1.0);

        const double vtn   = nf * 0.02585;
        const double scale = 1.0 / (2.0 * is * rs);

        for (int i = 0; i < n; ++i)
            out[i] = static_cast<float>(vtn * std::asinh(in[i] * scale));
    }

private:
    int isIndex = -1, nIndex = -1, rsIndex = -1;
};

/// A triode valve stage. Asymmetric: the grid conducts on positive excursions
/// and compresses them, while negative excursions cut off more abruptly.
class Triode final : public MonoElement
{
protected:
    void cacheIndices(const ElementType& type) override
    {
        muIndex   = controlIndex(type, "mu");
        biasIndex = controlIndex(type, "bias");
    }

    void processMono(const float* in, float* out, int n, const ProcessArgs& args) noexcept override
    {
        const float mu   = std::max(controlAt(args, muIndex, 100.0f), 1.0f);
        const float bias = controlAt(args, biasIndex, -1.5f);

        const float scale = 1.0f / (1.0f + mu * 0.01f);

        for (int i = 0; i < n; ++i)
        {
            const float vg = in[i] + bias;

            // Grid conduction above zero, cutoff below.
            float y;
            if (vg > 0.0f)
                y = std::tanh(vg * 0.7f) * 0.7f;
            else
                y = -std::tanh(-vg * 1.3f);

            out[i] = (y - std::tanh(bias * 1.3f) * (bias < 0.0f ? -1.0f : 1.0f)) * scale * mu * 0.01f;
        }
    }

private:
    int muIndex = -1, biasIndex = -1;
};

}  // namespace valis::elements

namespace valis {
namespace {
template <typename T> std::unique_ptr<DspElement> make() { return std::make_unique<T>(); }
}  // namespace

void registerTransfers(ElementRegistry& registry)
{
    registry.add("Tanh",      &make<elements::Tanh>);
    registry.add("HardClip",  &make<elements::HardClip>);
    registry.add("SinArcTan", &make<elements::SinArcTan>);
    registry.add("SoftSine",  &make<elements::SoftSine>);
    registry.add("Fold",      &make<elements::Fold>);
    registry.add("Diode",     &make<elements::Diode>);
    registry.add("DiodePair", &make<elements::DiodePair>);
    registry.add("Triode",    &make<elements::Triode>);
}
}  // namespace valis

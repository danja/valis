// tests/dsp/ElementBehaviourTest.cpp

#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

#include <juce_dsp/juce_dsp.h>

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

const Ontology& ontology()
{
    static const Ontology loaded = [] {
        Ontology o;
        std::vector<std::string> errors;
        const bool ok = o.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors);
        assert(ok);
        return o;
    }();
    return loaded;
}

/// Runs one element over a block, with control values taken from the ontology
/// defaults and then overridden by name.
struct Rig
{
    Rig(std::string_view className, double rate = 48000.0)
        : type(ontology().find(vocab::valTerm(std::string(className))))
    {
        assert(type != nullptr);
        element = makeDefaultRegistry().create(type->implementation);
        assert(element != nullptr);

        for (const auto* port : type->portsMatching(true, true))
            controls.push_back(static_cast<float>(port->defaultValue));

        numAudioIn  = static_cast<int>(type->portsMatching(true,  false).size());
        numAudioOut = static_cast<int>(type->portsMatching(false, false).size());
        numCtrlOut  = static_cast<int>(type->portsMatching(false, true ).size());

        element->prepare(*type, rate, 4096);
        element->reset();
    }

    void set(std::string_view symbol, float value)
    {
        int index = 0;
        for (const auto* port : type->portsMatching(true, true))
        {
            if (port->symbol == symbol) { controls[static_cast<std::size_t>(index)] = value; return; }
            ++index;
        }
        assert(false && "no such control port");
    }

    /// Feeds `input` through and returns the named audio output.
    std::vector<float> run(const std::vector<float>& input, std::string_view outPort = "out")
    {
        const int n = static_cast<int>(input.size());

        std::vector<std::vector<float>> ins(static_cast<std::size_t>(std::max(numAudioIn, 1)),
                                            std::vector<float>(static_cast<std::size_t>(n), 0.0f));
        std::vector<std::vector<float>> outs(static_cast<std::size_t>(std::max(numAudioOut, 1)),
                                             std::vector<float>(static_cast<std::size_t>(n), 0.0f));
        if (numAudioIn > 0)
            ins[0] = input;

        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        for (auto& v : ins)  inPtrs.push_back(v.data());
        for (auto& v : outs) outPtrs.push_back(v.data());

        std::vector<float> ctrlOut(static_cast<std::size_t>(std::max(numCtrlOut, 1)), 0.0f);

        ProcessArgs args;
        args.audioIn       = inPtrs.data();
        args.audioOut      = outPtrs.data();
        args.numAudioIn    = numAudioIn;
        args.numAudioOut   = numAudioOut;
        args.numSamples    = n;
        args.controlIn     = controls.data();
        args.numControlIn  = static_cast<int>(controls.size());
        args.controlOut    = ctrlOut.data();
        args.numControlOut = numCtrlOut;

        element->process(args);
        lastControlOut = ctrlOut;

        int index = 0;
        for (const auto* port : type->portsMatching(false, false))
        {
            if (port->symbol == outPort)
                return outs[static_cast<std::size_t>(index)];
            ++index;
        }
        return outs[0];
    }

    const ElementType* type;
    std::unique_ptr<DspElement> element;
    std::vector<float> controls, lastControlOut;
    int numAudioIn = 0, numAudioOut = 0, numCtrlOut = 0;
};

std::vector<float> sine(double frequency, double rate, int n, float amplitude = 1.0f)
{
    std::vector<float> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        out[static_cast<std::size_t>(i)] =
            amplitude * static_cast<float>(std::sin(2.0 * M_PI * frequency * i / rate));
    return out;
}

/// Magnitude spectrum, Hann windowed.
std::vector<float> spectrum(const std::vector<float>& signal)
{
    const int order = 12;                 // 4096
    const int size  = 1 << order;
    assert(static_cast<int>(signal.size()) >= size);

    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window(static_cast<std::size_t>(size),
                                               juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> buffer(static_cast<std::size_t>(size) * 2, 0.0f);
    std::copy(signal.begin(), signal.begin() + size, buffer.begin());
    window.multiplyWithWindowingTable(buffer.data(), static_cast<std::size_t>(size));

    fft.performFrequencyOnlyForwardTransform(buffer.data());
    buffer.resize(static_cast<std::size_t>(size) / 2);
    return buffer;
}

float energyAt(const std::vector<float>& bins, double frequency, double rate, int fftSize)
{
    const auto centre = static_cast<int>(std::round(frequency * fftSize / rate));
    float sum = 0.0f;
    for (int b = centre - 2; b <= centre + 2; ++b)
        if (b >= 0 && b < static_cast<int>(bins.size()))
            sum += bins[static_cast<std::size_t>(b)] * bins[static_cast<std::size_t>(b)];

    return sum;
}

// ---------------------------------------------------------------------------

/// ADAA must converge to the plain transfer function when the input barely
/// moves between samples - otherwise it is not the same nonlinearity.
///
/// It integrates the shape across the interval between successive samples, so
/// it also delays: one sample for second order, half a sample for first. The
/// reference has to be shifted to match, or the comparison measures the delay
/// rather than the accuracy.
void testAdaaMatchesTheShapeAtLowFrequency()
{
    const int n = 4096;
    const auto input = sine(20.0, 48000.0, n, 0.9f);

    Rig rig("Tanh");                 // the ontology asks Tanh for ADAA2
    rig.set("gain", 3.0f);
    const auto out = rig.run(input);

    assert(rig.element->latencyInSamples() == 1);

    double worst = 0.0;
    for (int i = 64; i < n; ++i)     // skip start-up
    {
        const double reference = std::tanh(3.0 * input[static_cast<std::size_t>(i - 1)]);
        worst = std::max(worst, std::abs(reference - out[static_cast<std::size_t>(i)]));
    }

    std::printf("  ADAA2 tanh vs direct at 20 Hz, 1 sample aligned: max error %.2e\n", worst);
    assert(worst < 1.0e-4);

    // First order lands half a sample late, so compare against a midpoint.
    Rig first("Tanh");
    first.set("gain", 3.0f);
    ElementType type = *first.type;
    type.antialiasing = vocab::valTerm("ADAA1");
    first.element->prepare(type, 48000.0, 4096);
    first.element->reset();
    const auto firstOut = first.run(input);

    assert(first.element->latencyInSamples() == 0);

    double worstFirst = 0.0;
    for (int i = 64; i < n; ++i)
    {
        const double a = std::tanh(3.0 * input[static_cast<std::size_t>(i)]);
        const double b = std::tanh(3.0 * input[static_cast<std::size_t>(i - 1)]);
        worstFirst = std::max(worstFirst, std::abs(0.5 * (a + b) - firstOut[static_cast<std::size_t>(i)]));
    }

    std::printf("  ADAA1 tanh vs direct at 20 Hz, half sample aligned: max error %.2e\n", worstFirst);
    assert(worstFirst < 1.0e-4);
}

/// The claim in docs/skream.md: ADAA suppresses the aliasing a nonlinearity
/// creates, without oversampling. Measured, not asserted.
void testAdaaReducesAliasing()
{
    const double rate = 48000.0;
    const int n = 4096;
    const double f0 = 5000.0;

    // tanh of a sine makes odd harmonics: 15k, 25k, 35k, 45k, 55k. Everything
    // above 24k folds back to 23k, 13k, 3k and 7k - bins no harmonic occupies,
    // so energy there is aliasing and nothing else.
    const double aliasBins[] = {3000.0, 7000.0, 13000.0, 23000.0};

    const auto input = sine(f0, rate, n, 1.0f);

    const auto measure = [&](std::string_view strategy) {
        Rig rig("Tanh");
        rig.set("gain", 8.0f);

        // Override the ontology's strategy by re-preparing against a copy.
        ElementType type = *rig.type;
        type.antialiasing = strategy.empty() ? std::string() : vocab::valTerm(std::string(strategy));
        rig.element->prepare(type, rate, 4096);
        rig.element->reset();

        const auto out = rig.run(input);
        const auto bins = spectrum(out);

        float total = 0.0f;
        for (const double f : aliasBins)
            total += energyAt(bins, f, rate, 4096);
        return total;
    };

    const float none  = measure("None");
    const float adaa1 = measure("ADAA1");
    const float adaa2 = measure("ADAA2");

    std::printf("  alias energy  none %.4g   ADAA1 %.4g (%.1fx)   ADAA2 %.4g (%.1fx)\n",
                none, adaa1, none / adaa1, adaa2, none / adaa2);

    assert(adaa1 < none);
    assert(adaa2 < none);
}

void testStateVariableSplitsTheSpectrum()
{
    const double rate = 48000.0;
    const int n = 4096;

    Rig rig("StateVariable");
    rig.set("cutoff", 1000.0f);

    // A tone well below the corner passes the lowpass and is stopped by the
    // highpass; a tone well above does the reverse. Both taps come from the
    // same state, which is why they are separate ports.
    const auto low  = sine(100.0, rate, n);
    const auto lowLp = rig.run(low, "lp");
    rig.element->reset();
    const auto lowHp = rig.run(low, "hp");

    const auto rms = [](const std::vector<float>& v) {
        double sum = 0.0;
        for (std::size_t i = v.size() / 2; i < v.size(); ++i) sum += double(v[i]) * v[i];
        return std::sqrt(sum / (double(v.size()) / 2.0));
    };

    assert(rms(lowLp) > 0.6);
    assert(rms(lowHp) < 0.1);

    rig.element->reset();
    const auto high = sine(10000.0, rate, n);
    const auto highLp = rig.run(high, "lp");
    rig.element->reset();
    const auto highHp = rig.run(high, "hp");

    assert(rms(highLp) < 0.1);
    assert(rms(highHp) > 0.6);
}

void testUnitDelayDelaysByExactlyOneSample()
{
    Rig rig("UnitDelay");
    const std::vector<float> impulse{1.0f, 0.0f, 0.0f, 0.0f};
    const auto out = rig.run(impulse);

    assert(out[0] == 0.0f);
    assert(out[1] == 1.0f);
    assert(out[2] == 0.0f);
    assert(rig.element->latencyInSamples() == 1);
}

/// The fine-grained device claim: a diode conducts one way, so it must clip
/// asymmetrically, while an antiparallel pair must not.
void testDiodeIsAsymmetricAndThePairIsNot()
{
    const int n = 2048;
    const auto input = sine(100.0, 48000.0, n, 1.0f);

    const auto extremes = [&](Rig& rig) {
        const auto out = rig.run(input);
        float lo = 0.0f, hi = 0.0f;
        for (std::size_t i = n / 4; i < out.size(); ++i)
        {
            lo = std::min(lo, out[i]);
            hi = std::max(hi, out[i]);
        }
        return std::pair{lo, hi};
    };

    // Conducting, the diode compresses the positive half toward its forward
    // voltage; reverse biased it is open and the negative half passes intact.
    Rig diode("Diode");
    const auto [dLo, dHi] = extremes(diode);
    std::printf("  Diode swing     %+.4f .. %+.4f  (asymmetry %.1fx)\n",
                dLo, dHi, std::abs(dLo) / dHi);
    assert(dHi > 0.3f && dHi < 0.9f);            // clipped near the forward drop
    assert(dLo < -0.95f);                        // passed
    assert(std::abs(dLo) > 1.5f * dHi);          // genuinely asymmetric
    // Neither half is railed against a clamp - this is a curve, not a clipper.
    assert(dHi < 0.999f);

    // The antiparallel pair clips both halves alike.
    Rig pair("DiodePair");
    const auto [pLo, pHi] = extremes(pair);
    std::printf("  DiodePair swing %+.4f .. %+.4f\n", pLo, pHi);
    assert(pHi > 0.3f && pHi < 0.9f);
    assert(std::abs(std::abs(pLo) - pHi) < 0.02f * pHi);   // symmetric
    assert(pHi < 0.999f);
}

void testExpanderGatesQuietSignal()
{
    const std::vector<float> input(256, 0.5f);

    Rig open("Expander");
    open.set("amount", 1.0f);          // detector says the input is loud
    const auto passed = open.run(input);
    assert(std::abs(passed[128] - 0.5f) < 1.0e-6f);

    Rig shut("Expander");
    shut.set("amount", 1.0e-6f);       // detector says near silence
    const auto gated = shut.run(input);
    assert(std::abs(gated[128]) < 0.05f);
}

void testEnvelopeFollowerTracksLevel()
{
    Rig rig("EnvelopeFollower", 48000.0);
    rig.set("attack", 1.0f);
    rig.set("release", 100.0f);

    const std::vector<float> loud(4096, 0.8f);
    rig.run(loud);
    const float tracked = rig.lastControlOut[0];

    std::printf("  EnvelopeFollower tracked %.4f of 0.8\n", tracked);
    assert(tracked > 0.7f && tracked <= 0.81f);
}

void testGainIsDecibels()
{
    Rig rig("Gain");
    rig.set("gain", -6.0f);
    const std::vector<float> input(64, 1.0f);
    const auto out = rig.run(input);

    assert(std::abs(out[0] - 0.5011872f) < 1.0e-4f);
}

}  // namespace

int main()
{
    testAdaaMatchesTheShapeAtLowFrequency();
    testAdaaReducesAliasing();
    testStateVariableSplitsTheSpectrum();
    testUnitDelayDelaysByExactlyOneSample();
    testDiodeIsAsymmetricAndThePairIsNot();
    testExpanderGatesQuietSignal();
    testEnvelopeFollowerTracksLevel();
    testGainIsDecibels();

    std::puts("ElementBehaviourTest PASSED");
    return 0;
}

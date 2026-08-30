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
        o.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
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

/// A naive saw aliases: its harmonics run past Nyquist and fold back onto
/// frequencies no harmonic occupies. PolyBLEP should put much less energy
/// there. Measured, like the ADAA claim.
void testOscillatorIsBandLimited()
{
    const double rate = 48000.0;
    const int n = 4096;

    // 3300 Hz is chosen so that the fold-back frequencies land nowhere near a
    // harmonic. Harmonics are 3300, 6600, 9900 ... 23100; everything above
    // Nyquist folds to 21600, 18300, 15000 and 11700, none of which is a
    // multiple of 3300. Energy in those bins is aliasing and nothing else.
    //
    // Getting this wrong is easy: with a 3 kHz fundamental the obvious-looking
    // bins at 15k and 21k are the fifth and seventh harmonics, and the test
    // then measures the harmonic series instead.
    const double f0 = 3300.0;
    const double aliasBins[] = {21600.0, 18300.0, 15000.0, 11700.0};

    const auto measure = [&](int shape)
    {
        Rig rig("Oscillator", rate);
        rig.set("frequency", static_cast<float>(f0));
        rig.set("shape", static_cast<float>(shape));

        const std::vector<float> silence(static_cast<std::size_t>(n), 0.0f);
        const auto out = rig.run(silence);
        const auto bins = spectrum(out);

        float alias = 0.0f;
        for (const double f : aliasBins)
            alias += energyAt(bins, f, rate, 4096);

        // Normalised against the fundamental, so this measures spectral purity
        // rather than level.
        const float fundamental = energyAt(bins, f0, rate, 4096);
        return fundamental > 0.0f ? alias / fundamental : 1.0f;
    };

    const float saw    = measure(1);
    const float square = measure(2);

    std::printf("  osc alias/fundamental   saw %.3e   square %.3e\n", saw, square);
    std::fflush(stdout);

    // Measured with the correction removed and restored: saw 4.68e-2 -> 2.68e-3
    // (17x), square 2.09e-2 -> 7.32e-4 (29x). The bounds sit between those, so
    // removing PolyBLEP fails this test while normal drift does not.
    assert(saw < 8.0e-3);
    assert(square < 4.0e-3);

    // A sine has no discontinuity, so it should be near-pure to begin with.
    assert(measure(0) < 1.0e-5);

    // The shapes must still be the shapes: a saw sweeps the full range.
    Rig rig("Oscillator", rate);
    rig.set("frequency", 100.0f);
    rig.set("shape", 1.0f);
    const std::vector<float> silence(2048, 0.0f);
    const auto out = rig.run(silence);

    float lo = 0.0f, hi = 0.0f;
    for (std::size_t i = 512; i < out.size(); ++i)
    {
        lo = std::min(lo, out[i]);
        hi = std::max(hi, out[i]);
    }
    assert(hi > 0.9f && lo < -0.9f);
}

/// CombFilter should ring at the frequency set by its control and decay over
/// time. Feed a single impulse and measure peak energy at the target frequency.
void testCombFilterRingsAtCorrectPitch()
{
    const double rate = 48000.0;
    const int n = 8192;
    const float targetHz = 440.0f;

    Rig rig("CombFilter", rate);
    rig.set("frequency", targetHz);
    rig.set("feedback",  0.95f);
    rig.set("damping",   0.05f);

    // Single-sample impulse as excitation, then silence.
    std::vector<float> impulse(static_cast<std::size_t>(n), 0.0f);
    impulse[0] = 1.0f;

    const auto out  = rig.run(impulse);
    const auto bins = spectrum(out);

    // A comb at 440 Hz resonates at 440, 880, 1320 ... but NOT at 600 Hz.
    const float atTarget    = energyAt(bins, targetHz,         rate, 4096);
    const float atHarmonic2 = energyAt(bins, targetHz * 2.0,  rate, 4096);
    const float atNonMode   = energyAt(bins, 600.0,           rate, 4096);  // between modes

    std::printf("  comb energy  f0=%.0f: %.3e  2f0=%.0f: %.3e  non-mode 600Hz: %.3e\n",
                targetHz, atTarget, targetHz * 2.0f, atHarmonic2, atNonMode);

    // Fundamental and second harmonic must both be present (comb, not bandpass).
    assert(atTarget    > 0.0f);
    assert(atHarmonic2 > 0.0f);
    // Non-harmonic bins must be substantially quieter than the resonances.
    assert(atTarget > atNonMode * 10.0f);
}

/// StiffString must ring after an impulse, and changing dispersion must change
/// the output (proving the allpass stages are active). The stretch per partial
/// at low audio frequencies is only a few Hz, so this test does not attempt to
/// measure exact bin positions — it only checks that the element functions.
void testStiffStringDispersionAffectsOutput()
{
    const double rate = 48000.0;
    const int n = 8192;
    const float f0 = 220.0f;

    std::vector<float> impulse(static_cast<std::size_t>(n), 0.0f);
    impulse[0] = 1.0f;

    const auto runWith = [&](float dispersion) -> std::vector<float>
    {
        Rig rig("StiffString", rate);
        rig.set("frequency",  f0);
        rig.set("feedback",   0.97f);
        rig.set("damping",    0.02f);
        rig.set("dispersion", dispersion);
        return rig.run(impulse);
    };

    const auto out0 = runWith(0.0f);
    const auto out1 = runWith(0.8f);

    float peak0 = 0.0f, peak1 = 0.0f, diff = 0.0f;
    for (std::size_t i = 0; i < out0.size(); ++i)
    {
        peak0 = std::max(peak0, std::abs(out0[i]));
        peak1 = std::max(peak1, std::abs(out1[i]));
        diff += std::abs(out0[i] - out1[i]);
    }

    std::printf("  stiff string peak(d=0)=%.4f  peak(d=0.8)=%.4f  total-diff=%.2f\n",
                peak0, peak1, diff);

    assert(peak0 > 0.01f);   // rings with no dispersion
    assert(peak1 > 0.01f);   // rings with dispersion
    assert(diff  > 1.0f);    // the two outputs differ (allpass is active)
}

/// ModalBank must output energy near the mode frequencies and be silent
/// without excitation.
void testModalBankResonatesAtModeFrequencies()
{
    const double rate = 48000.0;
    const int n = 8192;
    const float f0 = 200.0f;

    Rig rig("ModalBank", rate);
    rig.set("frequency",  f0);
    rig.set("decay",      2.0f);
    rig.set("brightness", 1.0f);
    rig.set("mode",       0.0f);   // marimba: ratios 1, 2.756, 5.404 ...

    // Impulse excites all modes simultaneously.
    std::vector<float> impulse(static_cast<std::size_t>(n), 0.0f);
    impulse[0] = 1.0f;
    const auto out  = rig.run(impulse);
    const auto bins = spectrum(out);

    // Energy at the fundamental and second marimba mode (2.756 × f0).
    const float atF0    = energyAt(bins, f0,             rate, 4096);
    const float atMode1 = energyAt(bins, f0 * 2.756,     rate, 4096);
    const float atHarm2 = energyAt(bins, f0 * 2.0,       rate, 4096);   // NOT a marimba mode

    std::printf("  modal bank at f0: %.3e  at 2.756*f0: %.3e  at 2*f0: %.3e\n",
                atF0, atMode1, atHarm2);

    assert(atF0    > 0.0f);
    assert(atMode1 > 0.0f);
    // Marimba modes should be detectable above the non-mode bins.
    assert(atMode1 > atHarm2 * 0.5f);

    // A fresh rig that never receives an impulse must be silent.
    {
        Rig freshRig("ModalBank", rate);
        freshRig.set("frequency",  f0);
        freshRig.set("decay",      2.0f);
        freshRig.set("brightness", 1.0f);
        freshRig.set("mode",       0.0f);

        const std::vector<float> silence(static_cast<std::size_t>(n), 0.0f);
        const auto silOut = freshRig.run(silence);
        float silPeak = 0.0f;
        for (float s : silOut) silPeak = std::max(silPeak, std::abs(s));
        assert(silPeak < 1e-6f);
    }
}

/// Reed must be silent below the threshold and self-oscillate above it.
void testReedSelfOscillatesAbovePressureThreshold()
{
    const double rate = 48000.0;
    // Run 2× sampleRate samples so the oscillation has time to develop.
    const int warmup = static_cast<int>(rate * 2);

    const auto runAndMeasure = [&](float pressure) -> float
    {
        Rig rig("Reed", rate);
        rig.set("frequency",  220.0f);
        rig.set("pressure",   pressure);
        rig.set("stiffness",  0.5f);
        rig.set("damping",    0.2f);

        const std::vector<float> silence(static_cast<std::size_t>(warmup), 0.0f);
        const auto out = rig.run(silence);

        // RMS of the final quarter — oscillation should be stable by then.
        float rms = 0.0f;
        const int tail = warmup / 4;
        for (int i = warmup - tail; i < warmup; ++i)
            rms += out[static_cast<std::size_t>(i)] * out[static_cast<std::size_t>(i)];
        return std::sqrt(rms / tail);
    };

    const float rmsZero = runAndMeasure(0.0f);  // no blowing force
    const float rmsHigh = runAndMeasure(0.5f);  // active playing range

    std::printf("  reed rms: pressure=0.0 -> %.4f  pressure=0.5 -> %.4f\n",
                rmsZero, rmsHigh);

    assert(rmsZero < 0.001f);   // zero pressure = no energy injected = silence
    assert(rmsHigh > 0.01f);    // playing pressure = self-sustained oscillation
}

/// OnePole must pass low frequencies and attenuate high frequencies in lowpass
/// mode, and vice versa in highpass mode.
void testOnePoleFiltersCorrectly()
{
    const double rate = 48000.0;
    const int n = 4096;

    auto rms = [](const std::vector<float>& v, int from) {
        double sum = 0.0;
        for (int i = from; i < static_cast<int>(v.size()); ++i)
            sum += v[static_cast<std::size_t>(i)] * v[static_cast<std::size_t>(i)];
        return static_cast<float>(std::sqrt(sum / (v.size() - static_cast<std::size_t>(from))));
    };

    // Lowpass mode: 100 Hz should pass, 10 kHz should be attenuated.
    {
        Rig rig("OnePole", rate);
        rig.set("cutoff", 1000.0f);
        rig.set("mode", 0.0f);  // 0 = lowpass

        const float lowRms  = rms(rig.run(sine(100.0,   rate, n)), n / 2);
        rig.element->reset();
        const float highRms = rms(rig.run(sine(10000.0, rate, n)), n / 2);

        std::printf("  onepole LP (cutoff 1kHz): 100Hz=%.3f  10kHz=%.3f\n", lowRms, highRms);
        assert(lowRms  > 0.5f);
        assert(highRms < lowRms * 0.1f);
    }

    // Highpass mode: 10 kHz should pass, 100 Hz should be attenuated.
    {
        Rig rig("OnePole", rate);
        rig.set("cutoff", 1000.0f);
        rig.set("mode", 1.0f);  // 1 = highpass

        const float highRms = rms(rig.run(sine(10000.0, rate, n)), n / 2);
        rig.element->reset();
        const float lowRms  = rms(rig.run(sine(100.0,   rate, n)), n / 2);

        std::printf("  onepole HP (cutoff 1kHz): 10kHz=%.3f  100Hz=%.3f\n", highRms, lowRms);
        assert(highRms > 0.5f);
        assert(lowRms  < highRms * 0.1f);
    }
}

/// Ladder must pass signals below its cutoff and attenuate signals above it.
/// At high resonance the filter rings, so an impulse is audible as a tone at
/// the cutoff frequency.
void testLadderFiltersAndResonates()
{
    const double rate = 48000.0;
    const int n = 4096;

    auto rms = [](const std::vector<float>& v, int from) {
        double sum = 0.0;
        for (int i = from; i < static_cast<int>(v.size()); ++i)
            sum += v[static_cast<std::size_t>(i)] * v[static_cast<std::size_t>(i)];
        return static_cast<float>(std::sqrt(sum / (v.size() - static_cast<std::size_t>(from))));
    };

    // Lowpass: DC (or very low frequency) should pass; Nyquist tone should be
    // stopped. This is true regardless of the exact cutoff-to-coefficient mapping.
    {
        Rig rig("Ladder", rate);
        rig.set("cutoff", 1000.0f);
        rig.set("resonance", 0.0f);

        // DC test: constant input should settle near the input value.
        const auto dc = std::vector<float>(static_cast<std::size_t>(n), 0.5f);
        const auto dcOut = rig.run(dc);
        const float dcRms = rms(dcOut, n / 2);

        rig.element->reset();

        // Nyquist test: alternating ±0.5 is heavily filtered.
        std::vector<float> nyq(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            nyq[static_cast<std::size_t>(i)] = (i % 2 == 0) ? 0.5f : -0.5f;
        const auto nyqOut = rig.run(nyq);
        const float nyqRms = rms(nyqOut, n / 2);

        std::printf("  ladder lowpass: DC rms=%.4f  Nyquist rms=%.6f\n", dcRms, nyqRms);
        assert(dcRms  > 0.4f);
        assert(nyqRms < dcRms * 0.01f);
    }

    // Resonance: a high-resonance filter should decay more slowly than a flat
    // one — the tail (last quarter) should contain more of the total energy
    // relative to the early part.
    {
        const std::vector<float> impulse = [&] {
            std::vector<float> v(static_cast<std::size_t>(n), 0.0f);
            v[0] = 0.5f;
            return v;
        }();

        Rig rigFlat("Ladder", rate);
        rigFlat.set("cutoff", 1000.0f);
        rigFlat.set("resonance", 0.0f);
        const auto flatOut = rigFlat.run(impulse);

        Rig rigRes("Ladder", rate);
        rigRes.set("cutoff", 1000.0f);
        rigRes.set("resonance", 0.95f);
        const auto resOut = rigRes.run(impulse);

        const float flatTail = rms(flatOut, n * 3 / 4);
        const float resTail  = rms(resOut,  n * 3 / 4);

        std::printf("  ladder resonance tail: flat=%.7f  resonant=%.7f\n", flatTail, resTail);
        assert(resTail > flatTail * 2.0f);  // resonance prolongs the decay
    }
}

/// Noise must produce non-zero, non-DC output across repeated calls — the
/// classic check that the source is running and not stuck.
void testNoiseIsNonZeroAndVaries()
{
    Rig rig("Noise");
    const int n = 1024;

    const auto block1 = rig.run(std::vector<float>(static_cast<std::size_t>(n), 0.0f));
    const auto block2 = rig.run(std::vector<float>(static_cast<std::size_t>(n), 0.0f));

    float peak = 0.0f;
    for (float s : block1) peak = std::max(peak, std::abs(s));
    assert(peak > 0.001f);  // non-zero

    float diff = 0.0f;
    for (int i = 0; i < n; ++i)
        diff += std::abs(block1[static_cast<std::size_t>(i)] - block2[static_cast<std::size_t>(i)]);
    assert(diff > 0.0f);  // non-constant across blocks

    std::printf("  noise peak=%.3f block-diff=%.3f\n", peak, diff / n);
}

/// LFO must produce an oscillating control output whose range spans positive
/// and negative values at the requested rate.
void testLfoOscillates()
{
    const double rate = 48000.0;
    const int blockSize = 512;
    const int numBlocks = static_cast<int>(rate / blockSize) * 2;  // 2 seconds

    Rig rig("LFO", rate);
    rig.set("rate", 2.0f);   // 2 Hz → period = 24000 samples = 46–47 blocks

    float maxOut = -1e9f, minOut = 1e9f;
    const std::vector<float> silence(static_cast<std::size_t>(blockSize), 0.0f);
    for (int b = 0; b < numBlocks; ++b)
    {
        rig.run(silence);
        maxOut = std::max(maxOut, rig.lastControlOut[0]);
        minOut = std::min(minOut, rig.lastControlOut[0]);
    }

    std::printf("  lfo 2Hz range: [%.3f, %.3f]\n", minOut, maxOut);
    assert(maxOut >  0.5f);
    assert(minOut < -0.5f);
}

/// VCA must scale audio by the cv value: cv=0 silences, cv=1 is unity gain.
void testVcaScalesByCV()
{
    const int n = 512;
    const auto signal = sine(440.0, 48000.0, n, 0.5f);

    auto rms = [](const std::vector<float>& v) {
        double sum = 0.0;
        for (float s : v) sum += s * s;
        return static_cast<float>(std::sqrt(sum / v.size()));
    };

    Rig rigOpen("VCA");
    rigOpen.set("cv", 1.0f);
    const float rmsOpen = rms(rigOpen.run(signal));

    Rig rigClosed("VCA");
    rigClosed.set("cv", 0.0f);
    const float rmsClosed = rms(rigClosed.run(signal));

    std::printf("  vca: cv=1.0 rms=%.4f  cv=0.0 rms=%.4f\n", rmsOpen, rmsClosed);
    assert(rmsOpen   > 0.3f);
    assert(rmsClosed < 1e-4f);
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
    testOscillatorIsBandLimited();
    testCombFilterRingsAtCorrectPitch();
    testStiffStringDispersionAffectsOutput();
    testModalBankResonatesAtModeFrequencies();
    testReedSelfOscillatesAbovePressureThreshold();
    testOnePoleFiltersCorrectly();
    testLadderFiltersAndResonates();
    testNoiseIsNonZeroAndVaries();
    testLfoOscillates();
    testVcaScalesByCV();

    std::puts("ElementBehaviourTest PASSED");
    return 0;
}

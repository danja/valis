// include/valis/ElementTestFixture.h
//
// Test fixture for examining signal behaviour (levels, frequency response,
// THD, transient response) of individual Valis circuit elements.

#pragma once

#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <string>
#include <string_view>
#include <vector>

namespace valis {

class ElementTestFixture
{
public:
    explicit ElementTestFixture(std::string_view className, double sampleRate = 48000.0)
        : rate(sampleRate)
    {
        const auto& ont = getOntology();
        type = ont.find(vocab::valTerm(std::string(className)));
        assert(type != nullptr && "Element class not found in ontology");

        const auto registry = makeDefaultRegistry();
        element = registry.create(type->implementation);
        assert(element != nullptr && "Element implementation factory not registered");

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
            if (port->symbol == symbol)
            {
                controls[static_cast<std::size_t>(index)] = value;
                return;
            }
            ++index;
        }
        assert(false && "no such control port");
    }

    void setNote(int note, float vel = 1.0f, bool g = true)
    {
        noteNumber = note;
        velocity   = vel;
        gate       = g;
    }

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
        args.gate          = gate;
        args.velocity      = velocity;
        args.noteNumber    = noteNumber;
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

    // Helper signal generators
    static std::vector<float> generateSine(double frequency, double rate, int n, float amplitude = 1.0f)
    {
        std::vector<float> out(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            out[static_cast<std::size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * frequency * i / rate));
        return out;
    }

    static std::vector<float> generateImpulse(int n, float amplitude = 1.0f)
    {
        std::vector<float> out(static_cast<std::size_t>(n), 0.0f);
        if (n > 0) out[0] = amplitude;
        return out;
    }

    static std::vector<float> generateStep(int n, float amplitude = 1.0f)
    {
        return std::vector<float>(static_cast<std::size_t>(n), amplitude);
    }

    // Signal analysis helper functions
    static float measurePeak(const std::vector<float>& signal)
    {
        float p = 0.0f;
        for (float s : signal) p = std::max(p, std::abs(s));
        return p;
    }

    static float measureRms(const std::vector<float>& signal, std::size_t startSample = 0)
    {
        if (startSample >= signal.size()) return 0.0f;
        double sum = 0.0;
        for (std::size_t i = startSample; i < signal.size(); ++i)
            sum += static_cast<double>(signal[i]) * signal[i];
        return static_cast<float>(std::sqrt(sum / static_cast<double>(signal.size() - startSample)));
    }

    static std::vector<float> computeSpectrum(const std::vector<float>& signal, int fftOrder = 12)
    {
        const int size = 1 << fftOrder;
        assert(static_cast<int>(signal.size()) >= size);

        juce::dsp::FFT fft(fftOrder);
        juce::dsp::WindowingFunction<float> window(static_cast<std::size_t>(size),
                                                   juce::dsp::WindowingFunction<float>::hann);

        std::vector<float> buffer(static_cast<std::size_t>(size) * 2, 0.0f);
        std::copy(signal.begin(), signal.begin() + size, buffer.begin());
        window.multiplyWithWindowingTable(buffer.data(), static_cast<std::size_t>(size));

        fft.performFrequencyOnlyForwardTransform(buffer.data());
        buffer.resize(static_cast<std::size_t>(size) / 2);
        return buffer;
    }

    static float measureEnergyAt(const std::vector<float>& bins, double frequency, double rate, int fftSize = 4096)
    {
        const auto centre = static_cast<int>(std::round(frequency * fftSize / rate));
        float sum = 0.0f;
        for (int b = centre - 2; b <= centre + 2; ++b)
            if (b >= 0 && b < static_cast<int>(bins.size()))
                sum += bins[static_cast<std::size_t>(b)] * bins[static_cast<std::size_t>(b)];
        return sum;
    }

    const ElementType* type = nullptr;
    std::unique_ptr<DspElement> element;
    std::vector<float> controls, lastControlOut;
    int numAudioIn = 0, numAudioOut = 0, numCtrlOut = 0;
    bool  gate       = false;
    float velocity   = 1.0f;
    int   noteNumber = 69;
    double rate      = 48000.0;

private:
    static const Ontology& getOntology()
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
};

}  // namespace valis

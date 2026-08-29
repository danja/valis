// tests/dsp/ElementRegistryTest.cpp

#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

/// The drift test, in both directions. The ontology is loaded at run time and
/// drives construction, so a class declared there with no factory - or a factory
/// with no class - is a bug that must fail the build, not a silent gap.
void testOntologyAndRegistryAgree()
{
    Ontology ontology;
    std::vector<std::string> errors;
    assert(ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors));
    assert(errors.empty());

    const auto registry = makeDefaultRegistry();

    const auto declared  = ontology.implementationKeys();
    const auto available = registry.keys();

    std::vector<std::string> missing, extra;
    std::set_difference(declared.begin(), declared.end(),
                        available.begin(), available.end(), std::back_inserter(missing));
    std::set_difference(available.begin(), available.end(),
                        declared.begin(), declared.end(), std::back_inserter(extra));

    for (const auto& key : missing)
        std::printf("  ontology declares val:implementation \"%s\" with no factory\n", key.c_str());
    for (const auto& key : extra)
        std::printf("  registry has factory \"%s\" with no ontology class\n", key.c_str());

    assert(missing.empty());
    assert(extra.empty());
    assert(declared == available);

    // And every declared class actually constructs.
    for (const auto* type : ontology.types())
    {
        auto element = registry.create(type->implementation);
        assert(element != nullptr);
        element->prepare(*type, 48000.0, 512);
        element->reset();
    }

    assert(registry.create("NotAnElement") == nullptr);
    assert(! registry.contains("NotAnElement"));
}

/// The registry test proves every declared class constructs. It does not prove
/// the declaration matches the implementation - val:Envelope declared an audio
/// output while writing a control one, and nothing caught it until a circuit
/// tried to use it.
///
/// So: run every element and check it writes the ports it declares.
void testEveryElementWritesWhatItDeclares()
{
    Ontology ontology;
    std::vector<std::string> errors;
    ontology.loadUnits(VALIS_VOCABS_DIR "/lv2/units.ttl", errors);
    assert(ontology.loadFile(VALIS_VOCABS_DIR "/valis.ttl", errors));

    const auto registry = makeDefaultRegistry();
    constexpr float sentinel = -12345.0f;
    constexpr int blockSize = 64;

    // val:Input and val:Output are markers: the engine fills one and reads the
    // other, so neither writes anything itself.
    const std::set<std::string> markers{"Input", "Output"};

    for (const auto* type : ontology.types())
    {
        if (markers.count(type->implementation) != 0)
            continue;

        auto element = registry.create(type->implementation);
        assert(element != nullptr);
        element->prepare(*type, 48000.0, blockSize);
        element->reset();

        const auto audioIns   = type->portsMatching(true,  false).size();
        const auto audioOuts  = type->portsMatching(false, false).size();
        const auto controlIns = type->portsMatching(true,  true).size();
        const auto controlOuts= type->portsMatching(false, true).size();

        // A signal with movement, so a filter or a detector has something to do.
        std::vector<std::vector<float>> ins(std::max<std::size_t>(audioIns, 1),
                                            std::vector<float>(blockSize));
        for (auto& buffer : ins)
            for (int i = 0; i < blockSize; ++i)
                buffer[static_cast<std::size_t>(i)] =
                    0.7f * std::sin(0.15f * static_cast<float>(i));

        std::vector<std::vector<float>> outs(std::max<std::size_t>(audioOuts, 1),
                                             std::vector<float>(blockSize, sentinel));

        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        for (auto& v : ins)  inPtrs.push_back(v.data());
        for (auto& v : outs) outPtrs.push_back(v.data());

        std::vector<float> controlValues;
        for (const auto* port : type->portsMatching(true, true))
            controlValues.push_back(static_cast<float>(port->defaultValue));

        std::vector<float> controlOut(std::max<std::size_t>(controlOuts, 1), sentinel);

        ProcessArgs args;
        args.audioIn       = inPtrs.data();
        args.audioOut      = outPtrs.data();
        args.numAudioIn    = static_cast<int>(audioIns);
        args.numAudioOut   = static_cast<int>(audioOuts);
        args.numSamples    = blockSize;
        args.gate          = true;      // so gated elements do something
        args.velocity      = 1.0f;
        args.controlIn     = controlValues.data();
        args.numControlIn  = static_cast<int>(controlIns);
        args.controlOut    = controlOut.data();
        args.numControlOut = static_cast<int>(controlOuts);

        element->process(args);

        const auto name = vocab::shortName(type->classIri);

        // Every declared audio output must be written, and written finitely.
        for (std::size_t p = 0; p < audioOuts; ++p)
        {
            bool written = false;
            for (int i = 0; i < blockSize; ++i)
            {
                const float value = outs[p][static_cast<std::size_t>(i)];
                if (value != sentinel)
                    written = true;
                assert(std::isfinite(value) || value == sentinel);
            }

            if (! written)
                std::printf("  val:%s declares audio output '%s' but never writes it\n",
                            name.c_str(), type->portsMatching(false, false)[p]->symbol.c_str());
            assert(written);
        }

        // And every declared control output.
        for (std::size_t p = 0; p < controlOuts; ++p)
        {
            if (controlOut[p] == sentinel)
                std::printf("  val:%s declares control output '%s' but never writes it\n",
                            name.c_str(), type->portsMatching(false, true)[p]->symbol.c_str());
            assert(controlOut[p] != sentinel);
            assert(std::isfinite(controlOut[p]));
        }
    }
}

}  // namespace

int main()
{
    testOntologyAndRegistryAgree();
    testEveryElementWritesWhatItDeclares();

    const auto registry = makeDefaultRegistry();
    std::printf("ElementRegistryTest PASSED (%zu elements)\n", registry.size());
    return 0;
}

// tests/dsp/ElementRegistryTest.cpp

#include "valis/DspElement.h"
#include "valis/Ontology.h"
#include "valis/Vocabulary.h"

#include <algorithm>
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

}  // namespace

int main()
{
    testOntologyAndRegistryAgree();

    const auto registry = makeDefaultRegistry();
    std::printf("ElementRegistryTest PASSED (%zu elements)\n", registry.size());
    return 0;
}

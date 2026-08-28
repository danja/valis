// tests/rdf/VocabularyTest.cpp

#include "valis/Vocabulary.h"

#include <cassert>
#include <cstdio>

using namespace valis::vocab;

int main()
{
    // One namespace, trailing slash.
    assert(VAL == "http://purl.org/stuff/valis/");
    assert(val::Filter == "http://purl.org/stuff/valis/Filter");

    // shortName maps an IRI to the bare name the code and the message data share.
    assert(shortName(val::Filter) == "Filter");
    assert(shortName(lv2::symbol) == "symbol");          // '#' namespace
    assert(shortName(rdf::type) == "type");
    assert(shortName("bare") == "bare");

    // expand resolves known prefixes and passes anything else through.
    assert(expand("val:Transfer") == val::Transfer);
    assert(expand("lv2:ControlPort") == lv2::ControlPort);
    assert(expand("units:unit") == units::unit);
    assert(expand("http://example.org/x") == "http://example.org/x");
    assert(expand("unknown:thing") == "unknown:thing");

    // The brief's NonLinear and our Transfer are distinct IRIs; the ontology
    // relates them with owl:equivalentClass rather than making them the same term.
    assert(val::NonLinear != val::Transfer);

    std::puts("VocabularyTest PASSED");
    return 0;
}

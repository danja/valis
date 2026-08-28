// src/rdf/Vocabulary.cpp

#include "valis/Vocabulary.h"

#include <array>
#include <utility>

namespace valis::vocab {

std::string shortName(std::string_view iri)
{
    const auto slash = iri.find_last_of('/');
    const auto hash  = iri.find_last_of('#');

    std::size_t cut = std::string_view::npos;
    if (slash != std::string_view::npos && hash != std::string_view::npos)
        cut = std::max(slash, hash);
    else if (slash != std::string_view::npos)
        cut = slash;
    else
        cut = hash;

    if (cut == std::string_view::npos)
        return std::string(iri);

    return std::string(iri.substr(cut + 1));
}

std::string expand(std::string_view curie)
{
    static const std::array<std::pair<std::string_view, std::string_view>, 7> prefixes{{
        {"val:",   VAL},
        {"rdf:",   RDF},
        {"rdfs:",  RDFS},
        {"owl:",   OWL},
        {"xsd:",   XSD},
        {"lv2:",   LV2},
        {"units:", UNITS},
    }};

    for (const auto& [prefix, ns] : prefixes)
        if (curie.rfind(prefix, 0) == 0)
            return std::string(ns) + std::string(curie.substr(prefix.size()));

    return std::string(curie);
}

}  // namespace valis::vocab

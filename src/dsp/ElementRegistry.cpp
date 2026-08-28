// src/dsp/ElementRegistry.cpp

#include "valis/DspElement.h"

#include <algorithm>

namespace valis {

void ElementRegistry::add(std::string key, Factory factory)
{
    const auto it = std::find_if(factories.begin(), factories.end(),
                                 [&](const auto& e) { return e.first == key; });
    if (it != factories.end())
        it->second = factory;      // upsert, so a test can override one element
    else
        factories.emplace_back(std::move(key), factory);
}

std::unique_ptr<DspElement> ElementRegistry::create(std::string_view key) const
{
    const auto it = std::find_if(factories.begin(), factories.end(),
                                 [&](const auto& e) { return e.first == key; });
    return it != factories.end() ? it->second() : nullptr;
}

bool ElementRegistry::contains(std::string_view key) const
{
    return std::any_of(factories.begin(), factories.end(),
                       [&](const auto& e) { return e.first == key; });
}

std::vector<std::string> ElementRegistry::keys() const
{
    std::vector<std::string> result;
    result.reserve(factories.size());
    for (const auto& [key, factory] : factories)
        result.push_back(key);

    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace valis

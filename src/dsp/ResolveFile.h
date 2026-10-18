// src/dsp/ResolveFile.h
//
// Where a path named in a circuit points. Only juce_core, so the UI can ask the
// same question the elements do without linking the audio formats.

#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <string_view>

namespace valis::dsp {

/// A relative path is resolved against the working directory first, then the
/// shipped examples, so a circuit can name a file next to itself.
inline juce::File resolveFile(std::string_view path)
{
    const juce::String text{std::string(path)};

    if (text.startsWith("~/"))
        return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                   .getChildFile(text.substring(2));

    if (juce::File::isAbsolutePath(text))
        return juce::File(text);

    const auto relative = juce::File::getCurrentWorkingDirectory().getChildFile(text);
    if (relative.existsAsFile())
        return relative;

    const auto shipped = juce::File(VALIS_EXAMPLES_DIR).getChildFile(text);
    if (shipped.existsAsFile())
        return shipped;

    const auto root = juce::File(VALIS_ROOT_DIR).getChildFile(text);
    if (root.existsAsFile())
        return root;

    return relative;
}

}  // namespace valis::dsp

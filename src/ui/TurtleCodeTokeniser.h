// src/ui/TurtleCodeTokeniser.h
//
// JUCE ships tokenisers for C++, Lua and XML only, so Turtle is ours to write.
// CodeTokeniser has exactly two virtuals; the scanning helpers are cribbed from
// juce_CPlusPlusCodeTokeniserFunctions.h and juce_XMLCodeTokeniser.cpp.

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace valis {

class TurtleCodeTokeniser final : public juce::CodeTokeniser
{
public:
    enum TokenType
    {
        tokenType_error = 0,
        tokenType_comment,
        tokenType_directive,     ///< @prefix, @base, PREFIX, BASE
        tokenType_iri,           ///< <http://...>
        tokenType_prefixedName,  ///< val:Filter
        tokenType_blankNode,     ///< _:b1 and [ ]
        tokenType_string,
        tokenType_number,
        tokenType_keyword,       ///< a, true, false
        tokenType_punctuation,
        tokenType_identifier
    };

    int readNextToken(juce::CodeDocument::Iterator& source) override;
    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override;

    /// A scheme that reads on a dark background, matching the editor's chrome.
    static juce::CodeEditorComponent::ColourScheme darkColourScheme();
};

}  // namespace valis

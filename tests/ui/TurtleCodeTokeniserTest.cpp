// tests/ui/TurtleCodeTokeniserTest.cpp
//
// The tokeniser runs on every repaint of the editor, over text the user is in
// the middle of typing. It must classify correctly, and it must always
// terminate - a highlighter that loops on an unterminated string freezes the UI.

#include "ui/TurtleCodeTokeniser.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace valis;

namespace {

std::vector<std::pair<int, std::string>> tokenise(const juce::String& text)
{
    juce::CodeDocument document;
    document.replaceAllContent(text);

    TurtleCodeTokeniser tokeniser;
    juce::CodeDocument::Iterator source(document);

    std::vector<std::pair<int, std::string>> tokens;
    const int guard = 10000;

    for (int i = 0; i < guard; ++i)
    {
        const auto before = source.getPosition();
        const int type = tokeniser.readNextToken(source);
        const auto after = source.getPosition();

        if (before >= document.getNumCharacters())
            break;

        if (after >= document.getNumCharacters() && after == before)
            break;

        // Every call must consume at least one character, or the editor hangs.
        assert(after > before);
        tokens.emplace_back(type,
                            document.getTextBetween(juce::CodeDocument::Position(document, before),
                                                    juce::CodeDocument::Position(document, after))
                                .trim().toStdString());
    }

    return tokens;
}

bool has(const std::vector<std::pair<int, std::string>>& tokens, int type, const std::string& text)
{
    for (const auto& [t, s] : tokens)
        if (t == type && s == text)
            return true;
    return false;
}

int countOf(const std::vector<std::pair<int, std::string>>& tokens, int type)
{
    int n = 0;
    for (const auto& [t, s] : tokens)
        if (t == type) ++n;
    return n;
}

void testClassifiesTurtle()
{
    const auto tokens = tokenise(R"(# a comment
@prefix val: <http://purl.org/stuff/valis/> .
:vcf a val:Ladder ;
     val:cutoff 800.0 ;
     rdfs:label "Scream Filter" .
:a1 a val:Arc ; val:from [ val:node :osc ] .
)");

    assert(has(tokens, TurtleCodeTokeniser::tokenType_comment, "# a comment"));
    assert(has(tokens, TurtleCodeTokeniser::tokenType_directive, "@prefix"));
    assert(has(tokens, TurtleCodeTokeniser::tokenType_iri, "<http://purl.org/stuff/valis/>"));
    assert(has(tokens, TurtleCodeTokeniser::tokenType_prefixedName, "val:Ladder"));
    assert(has(tokens, TurtleCodeTokeniser::tokenType_prefixedName, ":vcf"));
    assert(has(tokens, TurtleCodeTokeniser::tokenType_keyword, "a"));
    assert(has(tokens, TurtleCodeTokeniser::tokenType_number, "800.0"));
    assert(has(tokens, TurtleCodeTokeniser::tokenType_string, "\"Scream Filter\""));
    assert(has(tokens, TurtleCodeTokeniser::tokenType_blankNode, "["));
    assert(has(tokens, TurtleCodeTokeniser::tokenType_punctuation, ";"));

    // No part of well-formed Turtle should read as an error.
    for (const auto& [type, text] : tokens)
        if (type == TurtleCodeTokeniser::tokenType_error)
            std::printf("  unexpected error token: '%s'\n", text.c_str());
    assert(countOf(tokens, TurtleCodeTokeniser::tokenType_error) == 0);
}

void testNumbersAndLiterals()
{
    const auto tokens = tokenise(R"(:x val:a 2.52e-9 ; val:b -1.5 ; val:c true ; val:d """long
string""" .)");

    assert(has(tokens, TurtleCodeTokeniser::tokenType_number, "2.52e-9"));
    assert(has(tokens, TurtleCodeTokeniser::tokenType_number, "-1.5"));
    assert(has(tokens, TurtleCodeTokeniser::tokenType_keyword, "true"));
    assert(countOf(tokens, TurtleCodeTokeniser::tokenType_string) == 1);   // one long string
}

/// Half-written text is the normal case in an editor, not the exception.
void testMalformedInputTerminates()
{
    const char* fragments[] = {
        "\"unterminated",
        "\"\"\"unterminated long",
        "<http://no-closing-bracket",
        "<",
        "@",
        ":",
        "_",
        "_:",
        "^^",
        "@prefix val: <http://purl.org/stuff/valis/",
        ":x a val:Ladder ;",
        "\\",
        "'''",
        "# comment with no newline",
    };

    for (const char* fragment : fragments)
    {
        const auto tokens = tokenise(fragment);
        (void) tokens;   // the assertion is inside tokenise: every call advances
    }

    // A stray '<' that is not an IRI must read as an error rather than swallow
    // the rest of the document.
    const auto stray = tokenise("<not an iri> :x a val:Gain .");
    assert(has(stray, TurtleCodeTokeniser::tokenType_error, "<"));
    assert(has(stray, TurtleCodeTokeniser::tokenType_prefixedName, ":x"));
}

void testColourSchemeCoversEveryTokenType()
{
    TurtleCodeTokeniser tokeniser;
    const auto scheme = tokeniser.getDefaultColourScheme();

    // One entry per token type, or a token would paint in the default colour
    // and silently lose its highlighting.
    assert(scheme.types.size() == 11);
    for (const auto& type : scheme.types)
        assert(type.name.isNotEmpty());
}

}  // namespace

int main()
{
    testClassifiesTurtle();
    testNumbersAndLiterals();
    testMalformedInputTerminates();
    testColourSchemeCoversEveryTokenType();

    std::puts("TurtleCodeTokeniserTest PASSED");
    return 0;
}

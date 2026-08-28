// src/ui/TurtleCodeTokeniser.cpp

#include "ui/TurtleCodeTokeniser.h"

namespace valis {

namespace {

bool isNameStart(juce::juce_wchar c)
{
    return juce::CharacterFunctions::isLetter(c) || c == '_';
}

bool isNameChar(juce::juce_wchar c)
{
    return juce::CharacterFunctions::isLetterOrDigit(c)
        || c == '_' || c == '-' || c == '.' || c == '%';
}

void skipToEndOfLine(juce::CodeDocument::Iterator& source)
{
    while (source.peekNextChar() != 0 && source.peekNextChar() != '\n')
        source.skip();
}

/// A long string is """...""" or '''...'''; a short one ends at the matching
/// quote. Both may contain escapes, and an unterminated one must not run away.
void skipString(juce::CodeDocument::Iterator& source, juce::juce_wchar quote)
{
    if (source.peekNextChar() == quote)
    {
        source.skip();
        if (source.peekNextChar() == quote)
        {
            source.skip();   // long string: """ or '''
            int matched = 0;
            for (;;)
            {
                const auto c = source.nextChar();
                if (c == 0)
                    return;
                if (c == '\\')
                {
                    source.skip();
                    matched = 0;
                }
                else if (c == quote)
                {
                    if (++matched == 3)
                        return;
                }
                else
                {
                    matched = 0;
                }
            }
        }
        return;   // an empty short string
    }

    for (;;)
    {
        const auto c = source.nextChar();
        if (c == 0 || c == quote || c == '\n')
            return;
        if (c == '\\')
            source.skip();
    }
}

}  // namespace

int TurtleCodeTokeniser::readNextToken(juce::CodeDocument::Iterator& source)
{
    source.skipWhitespace();
    const auto first = source.peekNextChar();

    // Nothing left: not a token, and certainly not an error.
    if (first == 0)
    {
        source.skip();
        return tokenType_punctuation;
    }

    switch (first)
    {

        case '#':
            skipToEndOfLine(source);
            return tokenType_comment;

        case '@':
            source.skip();
            while (isNameChar(source.peekNextChar()))
                source.skip();
            return tokenType_directive;

        case '<':
        {
            // An IRI runs to the closing '>' and may not contain whitespace;
            // anything else is a stray '<' and should read as an error.
            const auto start = source;
            source.skip();
            for (;;)
            {
                const auto c = source.nextChar();
                if (c == '>')
                    return tokenType_iri;
                if (c == 0 || c == '\n' || c == ' ')
                {
                    source = start;
                    source.skip();
                    return tokenType_error;
                }
            }
        }

        case '"':
        case '\'':
            source.skip();
            skipString(source, first);
            return tokenType_string;

        case '_':
            if (source.peekNextChar() == '_')
            {
                const auto start = source;
                source.skip();
                if (source.peekNextChar() == ':')
                {
                    source.skip();
                    while (isNameChar(source.peekNextChar()))
                        source.skip();
                    return tokenType_blankNode;
                }
                source = start;
            }
            break;

        case '[':
        case ']':
            source.skip();
            return tokenType_blankNode;

        case '.':
        case ';':
        case ',':
        case '(':
        case ')':
        case '^':
            source.skip();
            if (first == '^' && source.peekNextChar() == '^')
                source.skip();
            return tokenType_punctuation;

        default:
            break;
    }

    if (juce::CharacterFunctions::isDigit(first) || first == '-' || first == '+')
    {
        source.skip();
        while (juce::CharacterFunctions::isDigit(source.peekNextChar())
               || source.peekNextChar() == '.'
               || source.peekNextChar() == 'e' || source.peekNextChar() == 'E'
               || source.peekNextChar() == '-' || source.peekNextChar() == '+')
            source.skip();
        return tokenType_number;
    }

    if (isNameStart(first) || first == ':')
    {
        const auto start = source;
        juce::String word;

        while (isNameChar(source.peekNextChar()))
        {
            word += source.peekNextChar();
            source.skip();
        }

        // A ':' makes it a prefixed name, whether or not there was a prefix.
        if (source.peekNextChar() == ':')
        {
            source.skip();
            while (isNameChar(source.peekNextChar()))
                source.skip();
            return tokenType_prefixedName;
        }

        if (word == "a" || word == "true" || word == "false"
            || word.equalsIgnoreCase("prefix") || word.equalsIgnoreCase("base"))
            return tokenType_keyword;

        if (word.isNotEmpty())
            return tokenType_identifier;

        source = start;
    }

    source.skip();
    return tokenType_error;
}

juce::CodeEditorComponent::ColourScheme TurtleCodeTokeniser::getDefaultColourScheme()
{
    return darkColourScheme();
}

juce::CodeEditorComponent::ColourScheme TurtleCodeTokeniser::darkColourScheme()
{
    static const struct { const char* name; juce::uint32 colour; } types[] = {
        {"Error",         0xffe06c75},
        {"Comment",       0xff6a7080},
        {"Directive",     0xffc678dd},
        {"IRI",           0xff61afef},
        {"Prefixed name", 0xff56b6c2},
        {"Blank node",    0xffd19a66},
        {"String",        0xff98c379},
        {"Number",        0xffd19a66},
        {"Keyword",       0xffc678dd},
        {"Punctuation",   0xff8b92a0},
        {"Identifier",    0xffabb2bf},
    };

    juce::CodeEditorComponent::ColourScheme scheme;
    for (const auto& type : types)
        scheme.set(type.name, juce::Colour(type.colour));

    return scheme;
}

}  // namespace valis

// src/shot/BpeTokenizer.cpp

#include "valis/BpeTokenizer.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>

namespace valis {

namespace {

std::string utf8(int codePoint)
{
    std::string s;
    if (codePoint < 0x80)
        s += static_cast<char>(codePoint);
    else
    {
        s += static_cast<char>(0xC0 | (codePoint >> 6));
        s += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
    return s;
}

bool isSpace(unsigned char c) { return c == ' ' || (c >= 9 && c <= 13); }
bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
bool isLetter(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128; }
int charClass(unsigned char c) { return isLetter(c) ? 0 : isDigit(c) ? 1 : 2; }

/// The GPT-2 pre-tokenizer: 's 't 're 've 'm 'll 'd, then runs of letters, digits or other symbols each with an optional
/// leading space, then whitespace.
std::vector<std::string> split(const std::string& t)
{
    static const char* contractions[] = {"'s", "'t", "'re", "'ve", "'m", "'ll", "'d"};
    std::vector<std::string> out;
    std::size_t i = 0;
    const std::size_t n = t.size();
    while (i < n)
    {
        bool matched = false;
        if (t[i] == '\'')
            for (const char* c : contractions)
            {
                const std::size_t len = std::char_traits<char>::length(c);
                if (t.compare(i, len, c) == 0) { out.push_back(c); i += len; matched = true; break; }
            }
        if (matched)
            continue;
        const std::size_t start = i;
        std::size_t j = i;
        if (t[j] == ' ' && j + 1 < n && ! isSpace(static_cast<unsigned char>(t[j + 1])))
            ++j;
        if (! isSpace(static_cast<unsigned char>(t[j])))
        {
            const int cls = charClass(static_cast<unsigned char>(t[j]));
            while (j < n && ! isSpace(static_cast<unsigned char>(t[j])) && charClass(static_cast<unsigned char>(t[j])) == cls)
                ++j;
        }
        else
        {
            while (j < n && isSpace(static_cast<unsigned char>(t[j])))
                ++j;
            if (j < n && j - start > 1)
                --j;                                   // \s+(?!\S): the last space belongs to the next word
        }
        out.push_back(t.substr(start, j - start));
        i = j;
    }
    return out;
}

}  // namespace

bool BpeTokenizer::load(const std::string& vocabJsonPath, const std::string& mergesPath, std::string& error)
{
    vocab.clear();
    ranks.clear();

    int n = 0;                                          // GPT-2 maps bytes to printable code points
    for (int b = 0; b < 256; ++b)
    {
        const bool printable = (b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255);
        byteToUnicode[b] = utf8(printable ? b : 256 + n++);
    }

    const juce::var json = juce::JSON::parse(juce::File(vocabJsonPath));
    auto* object = json.getDynamicObject();
    if (object == nullptr)
    {
        error = "cannot read " + vocabJsonPath;
        return false;
    }
    for (const auto& property : object->getProperties())
        vocab[property.name.toString().toStdString()] = static_cast<long long>(static_cast<int>(property.value));

    std::ifstream merges(mergesPath);
    if (! merges)
    {
        error = "cannot read " + mergesPath;
        vocab.clear();
        return false;
    }
    std::string line;
    int rank = 0;
    std::getline(merges, line);                         // "#version" header
    while (std::getline(merges, line))
    {
        const auto space = line.find(' ');
        if (space != std::string::npos)
            ranks[{line.substr(0, space), line.substr(space + 1)}] = rank++;
    }
    return true;
}

std::vector<std::string> BpeTokenizer::bpe(const std::string& word) const
{
    std::vector<std::string> parts;
    for (const char c : word)
        parts.push_back(byteToUnicode[static_cast<unsigned char>(c)]);
    while (parts.size() > 1)
    {
        int best = std::numeric_limits<int>::max();
        std::size_t at = 0;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i)
        {
            const auto it = ranks.find({parts[i], parts[i + 1]});
            if (it != ranks.end() && it->second < best) { best = it->second; at = i; }
        }
        if (best == std::numeric_limits<int>::max())
            break;
        parts[at] += parts[at + 1];
        parts.erase(parts.begin() + static_cast<long>(at) + 1);
    }
    return parts;
}

std::vector<long long> BpeTokenizer::encode(const std::string& text) const
{
    std::vector<long long> ids{0};                      // <s>
    for (const auto& word : split(text))
        for (const auto& piece : bpe(word))
        {
            const auto it = vocab.find(piece);
            ids.push_back(it != vocab.end() ? it->second : 3);   // <unk>
        }
    ids.push_back(2);                                   // </s>
    return ids;
}

}  // namespace valis

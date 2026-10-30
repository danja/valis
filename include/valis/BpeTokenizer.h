// include/valis/BpeTokenizer.h
//
// The text tokenizer of CLAP: byte-level byte-pair encoding as in GPT-2 and RoBERTa
// (https://huggingface.co/docs/transformers/tokenizer_summary#byte-pair-encoding-bpe), read from the model's
// vocab.json and merges.txt. A sentence becomes <s> tokens </s>. Pre-splitting follows the GPT-2 rule for ASCII text;
// bytes of 128 and over are treated as letters, which agrees with the original for ordinary prose.

#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace valis {

class BpeTokenizer
{
public:
    /// Returns false, with a reason, if a file is missing or malformed.
    bool load(const std::string& vocabJsonPath, const std::string& mergesPath, std::string& error);
    bool loaded() const { return ! vocab.empty(); }

    std::vector<long long> encode(const std::string& text) const;

private:
    std::vector<std::string> bpe(const std::string& word) const;

    std::unordered_map<std::string, long long> vocab;
    std::map<std::pair<std::string, std::string>, int> ranks;
    std::string byteToUnicode[256];
};

}  // namespace valis

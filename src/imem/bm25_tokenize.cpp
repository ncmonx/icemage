// 2026-11-03: see bm25_tokenize.hpp.
#include "bm25_tokenize.hpp"
#include <cstdlib>
#include <unordered_set>

namespace icmg::imem {

namespace {
// Same list the python ablation used (C:/Temp/membench/bm25abl.py).
const std::unordered_set<std::string>& stopwords() {
    static const std::unordered_set<std::string> s = {
        "a", "an", "the", "and", "or", "but", "if", "of", "to", "in", "on", "at", "by",
        "for", "with", "from", "as", "is", "are", "was", "were", "be", "been", "being",
        "am", "do", "does", "did", "have", "has", "had", "i", "me", "my", "we", "our",
        "you", "your", "he", "she", "it", "its", "they", "them", "their", "this", "that",
        "these", "those", "what", "which", "who", "whom", "when", "where", "why", "how",
        "can", "could", "would", "should", "will", "shall", "may", "might", "must", "not",
        "no", "so", "than", "too", "very", "just", "about", "into", "over", "under",
        "again", "then", "there", "here", "all", "any", "some", "such", "only", "own",
        "same", "other", "more", "most", "s", "t", "don"};
    return s;
}

bool wordByte(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c >= 0x80;
}
}  // namespace

bool isBm25Stopword(const std::string& lowerToken) {
    return stopwords().count(lowerToken) > 0;
}

bool bm25WordTokenizerFromEnv() {
    const char* e = std::getenv(kBm25TokenizerEnv);
    return !(e && std::string(e) == kBm25TokenizerLegacy);
}

std::vector<std::string> wordTokenize(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (cur.size() >= 2 && !isBm25Stopword(cur)) out.push_back(cur);
        cur.clear();
    };
    for (unsigned char c : text) {
        if (wordByte(c)) {
            cur += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a')
                                          : static_cast<char>(c);
        } else {
            flush();
        }
    }
    flush();
    return out;
}

}  // namespace icmg::imem

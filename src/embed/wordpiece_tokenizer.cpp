// Phase 34: WordPiece tokenizer implementation. BERT base-uncased.
//
// Algorithm:
//   1. Lower-case input.
//   2. Split on whitespace + punctuation (each punct char = own token).
//   3. For each word: greedy longest-prefix match in vocab, prefix
//      continuation pieces with "##". Emit [UNK] if first char unmatched.
//   4. Wrap with [CLS] ... [SEP], pad/truncate to max_len.
//
// Reference: BERT tokenizer.basic_tokenize + tokenizer.wordpiece_tokenize
//            (https://github.com/google-research/bert/blob/master/tokenization.py)
#include "wordpiece_tokenizer.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cctype>

namespace icmg::embed {

bool WordPieceTokenizer::loadVocab(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    int64_t idx = 0;
    while (std::getline(f, line)) {
        // Strip trailing \r (CRLF files).
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) vocab_[line] = idx;
        ++idx;
    }
    return !vocab_.empty();
}

std::string WordPieceTokenizer::toLower(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back((c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : (char)c);
    }
    return out;
}

static bool isPunct(unsigned char c) {
    // BERT-style: ASCII punctuation only (basic_tokenize).
    if ((c >= 33 && c <= 47) ||   // ! " # $ % & ' ( ) * + , - . /
        (c >= 58 && c <= 64) ||   // : ; < = > ? @
        (c >= 91 && c <= 96) ||   // [ \ ] ^ _ `
        (c >= 123 && c <= 126))   // { | } ~
        return true;
    return false;
}

std::vector<std::string> WordPieceTokenizer::splitWhitespacePunct(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : s) {
        if (std::isspace(c)) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else if (isPunct(c)) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            out.push_back(std::string(1, (char)c));
        } else {
            cur.push_back((char)c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

int64_t WordPieceTokenizer::lookupId(const std::string& token) const {
    auto it = vocab_.find(token);
    if (it != vocab_.end()) return it->second;
    return UNK_ID;
}

std::vector<int64_t> WordPieceTokenizer::wordpieceSplit(const std::string& word) const {
    std::vector<int64_t> out;
    if (word.empty()) return out;
    // Greedy longest-prefix match. Continuation pieces get "##" prefix.
    size_t start = 0;
    while (start < word.size()) {
        size_t end = word.size();
        std::string cur_match;
        bool found = false;
        while (end > start) {
            std::string sub = word.substr(start, end - start);
            if (start > 0) sub = "##" + sub;
            auto it = vocab_.find(sub);
            if (it != vocab_.end()) {
                out.push_back(it->second);
                cur_match = sub;
                found = true;
                break;
            }
            --end;
        }
        if (!found) {
            // Unknown char at start -> entire word is UNK.
            return {UNK_ID};
        }
        start = end;
    }
    return out;
}

WordPieceTokenizer::Tokens WordPieceTokenizer::encode(const std::string& text, int max_len) const {
    Tokens t;
    if (max_len < 2) max_len = 2;   // need [CLS] + [SEP]
    t.input_ids.reserve(max_len);
    t.attention_mask.reserve(max_len);

    t.input_ids.push_back(CLS_ID);

    auto words = splitWhitespacePunct(toLower(text));
    for (auto& w : words) {
        auto ids = wordpieceSplit(w);
        for (auto id : ids) {
            if ((int)t.input_ids.size() >= max_len - 1) break;   // leave room for [SEP]
            t.input_ids.push_back(id);
        }
        if ((int)t.input_ids.size() >= max_len - 1) break;
    }
    t.input_ids.push_back(SEP_ID);

    // Build attention mask: 1 for real tokens, 0 for pad.
    int real = (int)t.input_ids.size();
    while ((int)t.input_ids.size() < max_len) t.input_ids.push_back(PAD_ID);
    for (int i = 0; i < (int)t.input_ids.size(); ++i)
        t.attention_mask.push_back(i < real ? 1 : 0);

    return t;
}

WordPieceTokenizer::PairTokens
WordPieceTokenizer::encodePair(const std::string& a, const std::string& b, int max_len) const {
    auto pieces = [this](const std::string& s) {
        std::vector<int64_t> ids;
        for (auto& w : splitWhitespacePunct(toLower(s)))
            for (auto id : wordpieceSplit(w)) ids.push_back(id);
        return ids;
    };
    auto ta = pieces(a), tb = pieces(b);
    const size_t budget = max_len > 3 ? (size_t)max_len - 3 : 0;
    while (ta.size() + tb.size() > budget) {
        if (tb.size() >= ta.size() && !tb.empty()) tb.pop_back();
        else if (!ta.empty()) ta.pop_back();
        else break;
    }
    PairTokens p;
    p.input_ids.push_back(CLS_ID);
    p.input_ids.insert(p.input_ids.end(), ta.begin(), ta.end());
    p.input_ids.push_back(SEP_ID);
    const size_t firstLen = p.input_ids.size();
    p.input_ids.insert(p.input_ids.end(), tb.begin(), tb.end());
    p.input_ids.push_back(SEP_ID);
    p.attention_mask.assign(p.input_ids.size(), 1);
    p.token_type_ids.assign(p.input_ids.size(), 1);
    std::fill(p.token_type_ids.begin(), p.token_type_ids.begin() + firstLen, 0);
    return p;
}

} // namespace icmg::embed

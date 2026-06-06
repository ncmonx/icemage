// bpe_tokenizer.cpp — byte-pair-merge core + tiktoken rank loader.
#include "bpe_tokenizer.hpp"
#include <fstream>
#include <sstream>
#include <vector>
#include <climits>
#include <cctype>

namespace icmg::core {

size_t BpeTokenizer::mergeCount(const std::string& piece) const {
    const size_t n = piece.size();
    if (n == 0) return 0;
    if (n == 1) return 1;

    // Part boundaries: starts[i]..starts[i+1] is one part. Begin one byte each.
    std::vector<size_t> starts(n + 1);
    for (size_t i = 0; i <= n; ++i) starts[i] = i;

    // Repeatedly merge the adjacent pair with the lowest rank (tiktoken greedy).
    // Pre-token chunks are short (a word), so the O(n^2)-ish scan is cheap.
    while (starts.size() > 2) {
        int best_rank = INT_MAX;
        size_t best_i = SIZE_MAX;
        for (size_t i = 0; i + 2 < starts.size(); ++i) {
            std::string pair = piece.substr(starts[i], starts[i + 2] - starts[i]);
            auto it = ranks_.find(pair);
            if (it != ranks_.end() && it->second < best_rank) {
                best_rank = it->second;
                best_i = i;
            }
        }
        if (best_i == SIZE_MAX) break;             // nothing left to merge
        starts.erase(starts.begin() + best_i + 1); // fuse parts best_i and best_i+1
    }
    return starts.size() - 1;
}

namespace {
// Minimal base64 decode for tiktoken rank keys.
std::string b64decode(const std::string& in) {
    static const std::string T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int val = 0, bits = -8;
    std::string out;
    for (unsigned char c : in) {
        if (c == '=') break;
        auto pos = T.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + (int)pos;
        bits += 6;
        if (bits >= 0) { out.push_back(char((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}

}  // namespace

// cl100k-style pre-tokenizer (ASCII-faithful). Replicates the tiktoken regex
// alternation in order at each position: contractions | optional-lead+letters |
// 1-3 digits | optional-space+punct+newlines | whitespace (with the (?!\S)
// "leave the last space for the next word" rule). Unicode letters/digits beyond
// ASCII are treated as "other"; full Unicode parity would need a \p{}-capable
// engine. Proven against known tiktoken splits in test_bpe_tokenizer.cpp.
std::vector<std::string> BpeTokenizer::preTokenize(const std::string& s) {
    std::vector<std::string> out;
    const size_t n = s.size();
    auto L  = [&](size_t k){ return k < n && (unsigned char)s[k] < 128 && std::isalpha((unsigned char)s[k]); };
    auto D  = [&](size_t k){ return k < n && (unsigned char)s[k] < 128 && std::isdigit((unsigned char)s[k]); };
    auto WS = [&](size_t k){ if (k >= n) return false; char c = s[k];
                             return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; };
    auto NL = [&](size_t k){ return k < n && (s[k]=='\r' || s[k]=='\n'); };
    auto P  = [&](size_t k){ return k < n && !L(k) && !D(k) && !WS(k); };  // punct/symbol
    auto lc = [&](size_t k)->int{ return k < n ? std::tolower((unsigned char)s[k]) : 0; };

    size_t i = 0;
    while (i < n) {
        // 1) contractions: '  (s|t|m|d|ll|ve|re), case-insensitive
        if (s[i] == '\'') {
            int a = lc(i+1), b = lc(i+2);
            if ((a=='l'&&b=='l') || (a=='v'&&b=='e') || (a=='r'&&b=='e')) { out.push_back(s.substr(i,3)); i+=3; continue; }
            if (a=='s'||a=='t'||a=='m'||a=='d')                            { out.push_back(s.substr(i,2)); i+=2; continue; }
        }
        // 2) word: [^\r\n letter digit]? letter+   (optional 1 lead char iff a letter follows)
        {
            size_t j = i;
            if (!L(j) && !D(j) && s[j] != '\r' && s[j] != '\n' && L(j+1)) j++;
            if (L(j)) {
                size_t w = j; while (L(w)) w++;
                out.push_back(s.substr(i, w - i)); i = w; continue;
            }
        }
        // 3) number: 1-3 digits
        if (D(i)) {
            size_t w = i, c = 0; while (D(w) && c < 3) { ++w; ++c; }
            out.push_back(s.substr(i, w - i)); i = w; continue;
        }
        // 4) punct: ' '? punct+ [\r\n]*
        {
            size_t j = i;
            if (s[j] == ' ' && P(j+1)) j++;
            if (P(j)) {
                size_t w = j; while (P(w)) ++w; while (NL(w)) ++w;
                out.push_back(s.substr(i, w - i)); i = w; continue;
            }
        }
        // 5) whitespace: \s*[\r\n] | \s+(?!\S) | \s+
        {
            size_t w = i; while (WS(w) && !NL(w)) ++w;
            if (NL(w)) { ++w; out.push_back(s.substr(i, w - i)); i = w; continue; }
            size_t e = i; while (WS(e)) ++e;
            if (e < n && !WS(e) && (e - 1) > i) {            // (?!\S): leave last space for next word
                out.push_back(s.substr(i, (e - 1) - i)); i = e - 1; continue;
            }
            out.push_back(s.substr(i, e - i)); i = e; continue;
        }
    }
    return out;
}

size_t BpeTokenizer::countTokens(const std::string& text) const {
    if (!ready() || text.empty()) return 0;
    size_t total = 0;
    for (const auto& chunk : preTokenize(text)) total += mergeCount(chunk);
    return total;
}

bool BpeTokenizer::loadRanks(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ls(line);
        std::string b64, rankStr;
        if (!(ls >> b64 >> rankStr)) continue;
        try {
            int rank = std::stoi(rankStr);
            std::string bytes = b64decode(b64);
            if (!bytes.empty()) ranks_[bytes] = rank;
        } catch (...) { /* skip malformed line */ }
    }
    return !ranks_.empty();
}

}  // namespace icmg::core

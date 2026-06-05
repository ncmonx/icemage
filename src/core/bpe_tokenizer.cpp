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

// Coarse pre-tokenizer (step 1): split into runs of one char-class, and attach a
// single leading space to the following run (tiktoken encodes " word" as a unit).
// Not the full cl100k regex yet — refined when the real vocab lands.
std::vector<std::string> preTokenize(const std::string& text) {
    std::vector<std::string> out;
    size_t i = 0, n = text.size();
    auto cls = [](unsigned char c) -> int {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return 0; // ws
        if (c < 128 && std::isalpha(c)) return 1;                       // letters
        if (c < 128 && std::isdigit(c)) return 2;                       // digits
        return 3;                                                       // punct/other
    };
    while (i < n) {
        // optional single leading space glued to the next non-space run
        std::string chunk;
        if (text[i] == ' ') { chunk.push_back(' '); ++i; }
        if (i < n && text[i] != ' ') {
            int k = cls((unsigned char)text[i]);
            while (i < n && (unsigned char)text[i] != ' ' && cls((unsigned char)text[i]) == k)
                chunk.push_back(text[i++]);
        }
        if (!chunk.empty()) out.push_back(chunk);
        // consume any remaining whitespace run as its own chunk
        while (i < n && (text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
            out.emplace_back(1, text[i++]);
        }
        if (i < n && text[i] == ' ' && (i + 1 >= n || text[i + 1] == ' ')) {
            // runs of spaces beyond the single glued one -> own chunk
            std::string sp;
            while (i < n && text[i] == ' ') sp.push_back(text[i++]);
            out.push_back(sp);
        }
    }
    return out;
}
}  // namespace

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

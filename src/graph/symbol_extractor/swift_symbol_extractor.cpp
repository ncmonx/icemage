// Swift symbol extractor — REGEX-based (no tree-sitter grammar).
//
// Deliberate design (2026-06-11): alex-pinkus/tree-sitter-swift does NOT commit
// a generated parser.c upstream (it is produced in CI) and its scanner is
// complex — vendoring it cleanly is impractical. Swift declarations
// (class/struct/enum/protocol/extension/func/init) are cleanly regex-
// extractable, so we use the same lean pattern as csharp/kotlin: zero grammar,
// zero bloat. The .swift -> "swift" mapping already exists in scanner EXT_MAP;
// this registers the "swift" handler.

#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include <regex>
#include <sstream>
#include <cstdint>
#include <cstdio>

namespace icmg::graph {

static std::string sw_fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

static int sw_matchBrace(const std::string& src, size_t open_pos) {
    int depth = 0;
    bool in_str = false, in_line = false, in_blk = false;
    char prev = 0;
    for (size_t i = open_pos; i < src.size(); ++i) {
        char c = src[i];
        if (in_line) { if (c == '\n') in_line = false; prev = c; continue; }
        if (in_blk)  { if (c == '/' && prev == '*') in_blk = false; prev = c; continue; }
        if (in_str)  { if (c == '"' && prev != '\\') in_str = false; prev = c; continue; }
        if (c == '/' && i + 1 < src.size() && src[i+1] == '/') { in_line = true; prev = c; continue; }
        if (c == '/' && i + 1 < src.size() && src[i+1] == '*') { in_blk = true; prev = c; continue; }
        if (c == '"')  { in_str = true; prev = c; continue; }
        if (c == '{') ++depth;
        else if (c == '}') { --depth; if (depth == 0) return (int)(i + 1); }
        prev = c;
    }
    return -1;
}

static int sw_lineOf(const std::string& src, size_t off) {
    int line = 1;
    for (size_t i = 0; i < off && i < src.size(); ++i) if (src[i] == '\n') ++line;
    return line;
}

static void sw_trim(std::string& t) {
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' ||
                          t.back() == '\n' || t.back() == '\r')) t.pop_back();
}

class SwiftSymbolExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                       const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty()) return out;

        // ---- Types: class/struct/enum/protocol/extension --------------
        // `[mods] (class|struct|enum|protocol|extension) Name[<T>][: Bases] {`
        static const std::regex re_type(
            R"((?:\b(?:public|private|internal|fileprivate|open|final|static|indirect)\s+)*)"
            R"(\b(class|struct|enum|protocol|extension)\s+(\w+))"
            R"((?:\s*<[^>]*>)?)"
            R"((?:\s*:\s*([^\{\n]+))?)"
            R"(\s*\{)",
            std::regex::ECMAScript);

        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_type);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            Symbol sym;
            sym.kind = m[1].str();
            sym.name = m[2].str();
            sym.signature = m[0].str();
            sw_trim(sym.signature);
            size_t open_pos = m.position(0) + m.length(0) - 1;  // the '{'
            int close_idx = sw_matchBrace(src, open_pos);
            sym.line_start = sw_lineOf(src, m.position(0));
            sym.line_end   = close_idx > 0 ? sw_lineOf(src, (size_t)close_idx - 1)
                                           : sym.line_start;
            if (close_idx > 0)
                sym.body_hash = sw_fnv1a64(src.substr(open_pos, (size_t)close_idx - open_pos));

            if (m[3].matched) {
                std::string bases = m[3].str();
                std::stringstream ss(bases);
                std::string tok;
                while (std::getline(ss, tok, ',')) {
                    sw_trim(tok);
                    auto cut = tok.find('<');
                    if (cut != std::string::npos) tok = tok.substr(0, cut);
                    sw_trim(tok);
                    if (!tok.empty()) sym.bases.push_back(tok);
                }
            }
            out.push_back(std::move(sym));
        }

        // ---- Functions / initializers: `func name(` and `init(` -------
        static const std::regex re_fun(
            R"(\b(?:func\s+(\w+)|(init)\b)\s*(?:<[^>]*>\s*)?\()",
            std::regex::ECMAScript);
        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_fun);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            Symbol sym;
            sym.kind = "function";
            sym.name = m[1].matched ? m[1].str() : "init";
            sym.line_start = sw_lineOf(src, m.position(0));
            size_t open = src.find('{', m.position(0));
            size_t nl   = src.find('\n', m.position(0));
            // protocol method decls have no body (no '{' before next decl);
            // only brace-match when a '{' plausibly opens this func soon.
            if (open != std::string::npos &&
                (nl == std::string::npos || open < src.find('\n', open))) {
                int close_idx = sw_matchBrace(src, open);
                sym.line_end = close_idx > 0 ? sw_lineOf(src, (size_t)close_idx - 1)
                                             : sym.line_start;
            } else {
                sym.line_end = sym.line_start;
            }
            out.push_back(std::move(sym));
        }

        return out;
    }
};

ICMG_REGISTER_SYMBOL_EXTRACTOR("swift", SwiftSymbolExtractor);

}  // namespace icmg::graph

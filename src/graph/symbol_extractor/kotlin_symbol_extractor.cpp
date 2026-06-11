// Kotlin symbol extractor — REGEX-based (no tree-sitter grammar).
//
// Deliberate design (2026-06-11): the fwcd/tree-sitter-kotlin parser.c is
// 22-34 MB — vendoring it would bloat the repo + risk an MSVC choke on a single
// huge TU. Kotlin's declarations (class/interface/object/enum/fun) are cleanly
// regex-extractable, so we follow the SAME lean pattern as csharp_symbol_
// extractor.cpp / sql_symbol_extractor.cpp: zero grammar, zero bloat, good
// enough for symbol + base-type extraction. The .kt/.kts -> "kotlin" mapping
// already exists in scanner EXT_MAP; this just registers the "kotlin" handler.

#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include <regex>
#include <sstream>
#include <cstdint>
#include <cstdio>

namespace icmg::graph {

static std::string kt_fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

// Match `{` at open_pos -> index past the matching `}` (string/char/comment
// aware). -1 if unmatched. Kotlin uses // and /* */ like C; strings are " and
// """ (triple) and chars '. Triple-quote handled coarsely (treated as string
// toggle on each "); good enough for brace balance in practice.
static int kt_matchBrace(const std::string& src, size_t open_pos) {
    int depth = 0;
    bool in_str = false, in_char = false, in_line = false, in_blk = false;
    char prev = 0;
    for (size_t i = open_pos; i < src.size(); ++i) {
        char c = src[i];
        if (in_line) { if (c == '\n') in_line = false; prev = c; continue; }
        if (in_blk)  { if (c == '/' && prev == '*') in_blk = false; prev = c; continue; }
        if (in_str)  { if (c == '"' && prev != '\\') in_str = false; prev = c; continue; }
        if (in_char) { if (c == '\'' && prev != '\\') in_char = false; prev = c; continue; }
        if (c == '/' && i + 1 < src.size() && src[i+1] == '/') { in_line = true; prev = c; continue; }
        if (c == '/' && i + 1 < src.size() && src[i+1] == '*') { in_blk = true; prev = c; continue; }
        if (c == '"')  { in_str = true; prev = c; continue; }
        if (c == '\'') { in_char = true; prev = c; continue; }
        if (c == '{') ++depth;
        else if (c == '}') { --depth; if (depth == 0) return (int)(i + 1); }
        prev = c;
    }
    return -1;
}

static int kt_lineOf(const std::string& src, size_t off) {
    int line = 1;
    for (size_t i = 0; i < off && i < src.size(); ++i) if (src[i] == '\n') ++line;
    return line;
}

static void kt_trim(std::string& t) {
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' ||
                          t.back() == '\n' || t.back() == '\r')) t.pop_back();
}

class KotlinSymbolExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                       const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty()) return out;

        // ---- Types: class / interface / object / enum class -------------
        // `[mods] [enum ]class|interface|object Name[<T>][(ctor)][: Bases][{]`
        static const std::regex re_type(
            R"((?:\b(?:public|private|internal|protected|open|final|abstract|sealed|data|inner|annotation|value|companion)\s+)*)"
            R"(\b(enum\s+class|class|interface|object)\s+(\w+))"
            R"((?:\s*<[^>]*>)?)"                       // type params
            R"((?:\s*\([^)]*\))?)"                     // primary constructor
            R"((?:\s*:\s*([^\{\n]+))?)",               // bases (until { or EOL)
            std::regex::ECMAScript);

        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_type);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            Symbol sym;
            std::string kw = m[1].str();
            sym.kind = (kw.rfind("enum", 0) == 0) ? "enum" : kw;  // enum class -> enum
            sym.name = m[2].str();
            sym.signature = m[0].str();
            kt_trim(sym.signature);
            sym.line_start = kt_lineOf(src, m.position(0));

            // Body: find the next '{' at/after the match end on the same
            // construct; if present, brace-match for line_end + body hash.
            size_t after = m.position(0) + m.length(0);
            size_t brace = src.find_first_not_of(" \t", after);
            if (brace != std::string::npos && src[brace] == '{') {
                int close_idx = kt_matchBrace(src, brace);
                if (close_idx > 0) {
                    sym.line_end = kt_lineOf(src, (size_t)close_idx - 1);
                    sym.body_hash = kt_fnv1a64(src.substr(brace, (size_t)close_idx - brace));
                } else {
                    sym.line_end = sym.line_start;
                }
            } else {
                sym.line_end = sym.line_start;   // bodyless: class Foo(val x: Int)
            }

            // Bases: split on ',', strip generics + ctor-call parens.
            if (m[3].matched) {
                std::string bases = m[3].str();
                std::stringstream ss(bases);
                std::string tok;
                while (std::getline(ss, tok, ',')) {
                    kt_trim(tok);
                    auto cut = tok.find_first_of("<(");
                    if (cut != std::string::npos) tok = tok.substr(0, cut);
                    kt_trim(tok);
                    if (!tok.empty()) sym.bases.push_back(tok);
                }
            }
            out.push_back(std::move(sym));
        }

        // ---- Functions: `fun [<T>] [recv.]name(` -----------------------
        static const std::regex re_fun(
            R"(\bfun\s+(?:<[^>]*>\s*)?(?:[\w.]+\.)?(\w+)\s*\()",
            std::regex::ECMAScript);
        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_fun);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            Symbol sym;
            sym.kind = "function";
            sym.name = m[1].str();
            sym.line_start = kt_lineOf(src, m.position(0));
            // Body: '{' (block) brace-matched; '=' (expression) single line.
            size_t after = m.position(0);
            size_t open = src.find('{', after);
            size_t eq   = src.find('=', after);
            size_t nl   = src.find('\n', after);
            if (open != std::string::npos && (eq == std::string::npos || open < eq) &&
                (nl == std::string::npos || open < nl + 1)) {
                int close_idx = kt_matchBrace(src, open);
                sym.line_end = close_idx > 0 ? kt_lineOf(src, (size_t)close_idx - 1)
                                             : sym.line_start;
            } else {
                sym.line_end = sym.line_start;   // expression body / abstract
            }
            out.push_back(std::move(sym));
        }

        return out;
    }
};

ICMG_REGISTER_SYMBOL_EXTRACTOR("kotlin", KotlinSymbolExtractor);

}  // namespace icmg::graph

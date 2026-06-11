// Scala symbol extractor — REGEX-based (no tree-sitter grammar).
//
// Same lean rationale as csharp/kotlin/swift/ruby. Scala uses braces for
// bodies (the common style), so we brace-match like the Kotlin extractor.
// Bases come from `extends X with Y with Z`. The .scala/.sc -> "scala" map
// already exists in scanner EXT_MAP; this registers the "scala" handler.
// (Scala 3 optional-braces / significant-indentation blocks fall back to a
// single-line range — acceptable for a best-effort symbol scan.)

#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include <regex>
#include <sstream>
#include <cstdint>
#include <cstdio>

namespace icmg::graph {

static std::string sc_fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

static int sc_matchBrace(const std::string& src, size_t open_pos) {
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

static int sc_lineOf(const std::string& src, size_t off) {
    int line = 1;
    for (size_t i = 0; i < off && i < src.size(); ++i) if (src[i] == '\n') ++line;
    return line;
}

static void sc_trim(std::string& t) {
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' ||
                          t.back() == '\n' || t.back() == '\r')) t.pop_back();
}

class ScalaSymbolExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                       const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty()) return out;

        // ---- class / object / trait ------------------------------------
        static const std::regex re_type(
            R"((?:\b(?:abstract|final|sealed|case|implicit|private|protected)\s+)*)"
            R"(\b(class|object|trait)\s+(\w+))"
            R"((?:\s*\[[^\]]*\])?)"                 // type params
            R"((?:\s*\([^)]*\))?)"                  // primary ctor (case class)
            R"((?:\s+extends\s+([^\{\n]+))?)",      // extends + with chain
            std::regex::ECMAScript);
        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_type);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            Symbol sym;
            sym.kind = m[1].str();                 // class | object | trait
            sym.name = m[2].str();
            sym.signature = m[0].str(); sc_trim(sym.signature);
            sym.line_start = sc_lineOf(src, m.position(0));

            size_t after = m.position(0) + m.length(0);
            size_t brace = src.find_first_not_of(" \t", after);
            if (brace != std::string::npos && src[brace] == '{') {
                int close_idx = sc_matchBrace(src, brace);
                if (close_idx > 0) {
                    sym.line_end = sc_lineOf(src, (size_t)close_idx - 1);
                    sym.body_hash = sc_fnv1a64(src.substr(brace, (size_t)close_idx - brace));
                } else sym.line_end = sym.line_start;
            } else sym.line_end = sym.line_start;

            // Bases: `Base with Mixin with Other` -> split on " with ".
            if (m[3].matched) {
                std::string ext = m[3].str(); sc_trim(ext);
                static const std::regex re_with(R"(\s+with\s+)", std::regex::ECMAScript);
                std::sregex_token_iterator ti(ext.begin(), ext.end(), re_with, -1), te;
                for (; ti != te; ++ti) {
                    std::string b = *ti; sc_trim(b);
                    auto cut = b.find_first_of("[(");
                    if (cut != std::string::npos) b = b.substr(0, cut);
                    sc_trim(b);
                    if (!b.empty()) sym.bases.push_back(b);
                }
            }
            out.push_back(std::move(sym));
        }

        // ---- methods: `def name` ---------------------------------------
        static const std::regex re_def(
            R"(\bdef\s+(\w+))",
            std::regex::ECMAScript);
        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_def);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            Symbol sym;
            sym.kind = "method";
            sym.name = m[1].str();
            sym.line_start = sc_lineOf(src, m.position(0));
            size_t nl = src.find('\n', m.position(0));
            size_t brace = src.find('{', m.position(0));
            if (brace != std::string::npos && (nl == std::string::npos || brace < src.find('\n', brace))) {
                int close_idx = sc_matchBrace(src, brace);
                sym.line_end = close_idx > 0 ? sc_lineOf(src, (size_t)close_idx - 1)
                                             : sym.line_start;
            } else {
                sym.line_end = sym.line_start;   // expression body `def x = ...`
            }
            out.push_back(std::move(sym));
        }

        return out;
    }
};

ICMG_REGISTER_SYMBOL_EXTRACTOR("scala", ScalaSymbolExtractor);

}  // namespace icmg::graph

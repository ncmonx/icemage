// Ruby symbol extractor — REGEX-based (no tree-sitter grammar).
//
// Same lean rationale as csharp/kotlin/swift: zero grammar, zero bloat. Ruby
// uses `end` (not braces) to close blocks, so instead of brace-matching we use
// an INDENTATION heuristic: a block's closing `end` aligns at the same column
// as its opener (true for conventionally-formatted Ruby). Good enough for
// symbol line ranges. The .rb/.rake/.gemspec -> "ruby" map already exists in
// scanner EXT_MAP; this registers the "ruby" handler.

#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include <regex>
#include <sstream>
#include <cstdint>
#include <cstdio>

namespace icmg::graph {

static std::string rb_fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

static int rb_lineOf(const std::string& src, size_t off) {
    int line = 1;
    for (size_t i = 0; i < off && i < src.size(); ++i) if (src[i] == '\n') ++line;
    return line;
}

// Column (indent width) of the line containing `off` — count leading spaces
// (tab counts as 1). Returns the indent of the opener keyword's line.
static int rb_indentAt(const std::string& src, size_t off) {
    size_t bol = src.rfind('\n', off == 0 ? 0 : off - 1);
    bol = (bol == std::string::npos) ? 0 : bol + 1;
    int indent = 0;
    for (size_t i = bol; i < src.size() && (src[i] == ' ' || src[i] == '\t'); ++i) ++indent;
    return indent;
}

// Find the matching `end` for a block opener at decl_off: first subsequent line
// whose first token is `end` at indent <= opener-indent. Returns end-of-that-
// line offset, or src.size() if none.
static size_t rb_matchEnd(const std::string& src, size_t decl_off, int opener_indent) {
    size_t i = src.find('\n', decl_off);
    if (i == std::string::npos) return src.size();
    ++i;
    while (i < src.size()) {
        size_t bol = i;
        int indent = 0;
        while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) { ++i; ++indent; }
        // first token
        size_t tok_start = i;
        while (i < src.size() && (isalnum((unsigned char)src[i]) || src[i] == '_')) ++i;
        std::string tok = src.substr(tok_start, i - tok_start);
        if (tok == "end" && indent <= opener_indent) {
            size_t eol = src.find('\n', bol);
            return eol == std::string::npos ? src.size() : eol;
        }
        // advance to next line
        size_t nl = src.find('\n', bol);
        if (nl == std::string::npos) break;
        i = nl + 1;
    }
    return src.size();
}

static void rb_trim(std::string& t) {
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' ||
                          t.back() == '\n' || t.back() == '\r')) t.pop_back();
}

class RubySymbolExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                       const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty()) return out;

        // ---- class / module --------------------------------------------
        // `class Name [< Base]` or `module Name`
        static const std::regex re_type(
            R"((?:^|\n)\s*(class|module)\s+([A-Z][\w:]*)(?:\s*<\s*([\w:]+))?)",
            std::regex::ECMAScript);
        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_type);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            size_t kw_off = m.position(0) + m[0].str().find(m[1].str());
            Symbol sym;
            sym.kind = m[1].str();                 // class | module
            sym.name = m[2].str();
            sym.line_start = rb_lineOf(src, kw_off);
            int indent = rb_indentAt(src, kw_off);
            size_t end_off = rb_matchEnd(src, kw_off, indent);
            sym.line_end = rb_lineOf(src, end_off);
            sym.body_hash = rb_fnv1a64(src.substr(kw_off, end_off - kw_off));
            if (m[3].matched) {
                std::string b = m[3].str(); rb_trim(b);
                if (!b.empty()) sym.bases.push_back(b);
            }
            out.push_back(std::move(sym));
        }

        // ---- methods: `def [self.]name` --------------------------------
        static const std::regex re_def(
            R"((?:^|\n)\s*def\s+(?:self\.)?([\w?!=]+))",
            std::regex::ECMAScript);
        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_def);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            size_t kw_off = m.position(0) + m[0].str().find("def");
            Symbol sym;
            sym.kind = "method";
            sym.name = m[1].str();
            sym.line_start = rb_lineOf(src, kw_off);
            int indent = rb_indentAt(src, kw_off);
            size_t end_off = rb_matchEnd(src, kw_off, indent);
            sym.line_end = rb_lineOf(src, end_off);
            out.push_back(std::move(sym));
        }

        return out;
    }
};

ICMG_REGISTER_SYMBOL_EXTRACTOR("ruby", RubySymbolExtractor);

}  // namespace icmg::graph

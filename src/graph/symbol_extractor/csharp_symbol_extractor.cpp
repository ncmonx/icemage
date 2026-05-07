#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include <regex>
#include <sstream>
#include <cstdint>
#include <unordered_set>

namespace icmg::graph {

// FNV1a 64-bit body hash (matches scanner's file hash style).
static std::string fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

// Find matching closing brace for `{` at body_start (returns index past `}`).
// Returns -1 if unmatched.
static int matchBrace(const std::string& src, size_t open_pos) {
    int depth = 0;
    bool in_str = false, in_char = false, in_line_cmt = false, in_blk_cmt = false;
    char prev = 0;
    for (size_t i = open_pos; i < src.size(); ++i) {
        char c = src[i];
        if (in_line_cmt) { if (c == '\n') in_line_cmt = false; prev = c; continue; }
        if (in_blk_cmt)  { if (c == '/' && prev == '*') in_blk_cmt = false; prev = c; continue; }
        if (in_str)      { if (c == '"' && prev != '\\') in_str = false; prev = c; continue; }
        if (in_char)     { if (c == '\'' && prev != '\\') in_char = false; prev = c; continue; }
        if (c == '/' && i + 1 < src.size() && src[i+1] == '/') { in_line_cmt = true; prev = c; continue; }
        if (c == '/' && i + 1 < src.size() && src[i+1] == '*') { in_blk_cmt = true; prev = c; continue; }
        if (c == '"')  { in_str = true; prev = c; continue; }
        if (c == '\'') { in_char = true; prev = c; continue; }
        if (c == '{') ++depth;
        else if (c == '}') { --depth; if (depth == 0) return (int)(i + 1); }
        prev = c;
    }
    return -1;
}

// Convert byte offset → 1-indexed line number.
static int lineOf(const std::string& src, size_t off) {
    int line = 1;
    for (size_t i = 0; i < off && i < src.size(); ++i) if (src[i] == '\n') ++line;
    return line;
}

class CSharpSymbolExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty()) return out;

        // Class / interface / struct / record (with optional base clause).
        // Matches: `[modifiers] class Name [: Base, IBase] {`
        static const std::regex re_type(
            R"((?:\b(?:public|private|internal|protected|static|sealed|abstract|partial)\s+)*)"
            R"(\b(class|interface|struct|record)\s+(\w+)(?:\s*<[^>]*>)?)"
            R"((?:\s*:\s*([^\{]+?))?\s*\{)",
            std::regex::ECMAScript);

        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_type);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            size_t open_pos = m.position(0) + m.length(0) - 1;  // the '{'
            int close_idx = matchBrace(src, open_pos);
            if (close_idx < 0) continue;

            Symbol sym;
            sym.kind = m[1].str();
            sym.name = m[2].str();
            sym.signature = m[0].str();
            sym.line_start = lineOf(src, m.position(0));
            sym.line_end   = lineOf(src, (size_t)close_idx - 1);
            std::string body = src.substr(open_pos, (size_t)close_idx - open_pos);
            sym.body_hash = fnv1a64(body);

            // Bases: split on `,` after stripping `where ...` constraints.
            if (m[3].matched) {
                std::string bases_str = m[3].str();
                auto wpos = bases_str.find(" where ");
                if (wpos != std::string::npos) bases_str = bases_str.substr(0, wpos);
                std::stringstream ss(bases_str);
                std::string tok;
                while (std::getline(ss, tok, ',')) {
                    while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t')) tok.erase(tok.begin());
                    while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t' || tok.back() == '\n' || tok.back() == '\r')) tok.pop_back();
                    auto ang = tok.find('<');
                    if (ang != std::string::npos) tok = tok.substr(0, ang);
                    if (!tok.empty()) sym.bases.push_back(tok);
                }
            }
            out.push_back(std::move(sym));
        }

        // Methods inside any class/struct: heuristic — `[mods] [type] Name(args) [body-or-=>]`.
        // Avoid matching control flow (if/for/while/switch/using/return/etc.).
        static const std::unordered_set<std::string> kw = {
            "if","for","while","switch","using","return","throw","catch","else",
            "try","finally","foreach","do","lock","yield","new","await",
            "get","set","add","remove","operator","case","when"
        };
        // Match: keyword sequences then `Name(...)` followed by `{` or `=>`.
        static const std::regex re_method(
            R"(([A-Za-z_]\w*)\s*\([^()]*\)\s*(?:where[^=\{]*)?\s*(\{|=>))",
            std::regex::ECMAScript);

        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_method);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            std::string name = m[1].str();
            if (kw.count(name)) continue;

            // Reject if the char before `(` is part of attribute use like `[Attr(...)`.
            size_t pos = m.position(0);
            // walk back over whitespace + a possible return type token
            // simple guard: require something on the same line before name (return type/modifier)
            size_t bol = src.rfind('\n', pos);
            if (bol == std::string::npos) bol = 0; else ++bol;
            std::string line_prefix = src.substr(bol, pos - bol);
            // skip if line starts with '[' (attribute)
            size_t lps = 0;
            while (lps < line_prefix.size() && (line_prefix[lps] == ' ' || line_prefix[lps] == '\t')) ++lps;
            if (lps < line_prefix.size() && line_prefix[lps] == '[') continue;
            // require at least one space-separated token in prefix (return type / modifier)
            bool has_modifier = false;
            for (char c : line_prefix) if (c == ' ' || c == '\t') { has_modifier = true; break; }
            if (!has_modifier) continue;

            Symbol sym;
            sym.kind = "method";
            sym.name = name;
            sym.line_start = lineOf(src, m.position(0));

            std::string body;
            if (m[2].str() == "{") {
                size_t open_pos = m.position(0) + m.length(0) - 1;
                int close_idx = matchBrace(src, open_pos);
                if (close_idx < 0) continue;
                sym.line_end = lineOf(src, (size_t)close_idx - 1);
                body = src.substr(open_pos, (size_t)close_idx - open_pos);
            } else {
                // expression-bodied: `=> expr;`
                size_t semi = src.find(';', m.position(0) + m.length(0));
                if (semi == std::string::npos) continue;
                sym.line_end = lineOf(src, semi);
                body = src.substr(m.position(0) + m.length(0), semi - (m.position(0) + m.length(0)));
            }
            sym.body_hash = fnv1a64(body);

            // Capture top-level signature line (prefix + match)
            sym.signature = line_prefix + m[0].str();

            // Calls: identifiers followed by `(`, excluding self + keywords.
            static const std::regex re_call(R"(\b([A-Za-z_]\w*)\s*\()");
            std::unordered_set<std::string> seen;
            for (auto cit = std::sregex_iterator(body.begin(), body.end(), re_call);
                 cit != std::sregex_iterator(); ++cit) {
                std::string callee = (*cit)[1].str();
                if (kw.count(callee)) continue;
                if (callee == name) continue;  // self-call
                if (seen.insert(callee).second) sym.calls.push_back(callee);
            }
            out.push_back(std::move(sym));
        }
        return out;
    }
};

ICMG_REGISTER_SYMBOL_EXTRACTOR("csharp", CSharpSymbolExtractor);
// Alias: source files extension `.cs` lang ID
namespace { struct _AliasCS {
    _AliasCS() {
        icmg::core::Registry<icmg::graph::BaseSymbolExtractor>::instance().reg("cs",
            []() -> std::unique_ptr<icmg::graph::BaseSymbolExtractor> {
                return std::make_unique<CSharpSymbolExtractor>();
            });
    }
} _alias_cs_inst; }

} // namespace icmg::graph

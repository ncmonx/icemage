// tree-sitter Dart AST symbol extractor.
//
// Compiled when ICMG_HAS_TREESITTER_DART is defined (CMake -DICMG_USE_TREESITTER=ON
// with vendored tree-sitter-dart grammar at third_party/tree-sitter-dart/src).
// Recursively walks the tree picking up declarations: class / enum / mixin /
// extension definitions and function/getter/setter/constructor signatures
// (method_signature wraps these, so natural recursion reaches each once).
// Call edges are left to the regex graph extractor — Dart has no single clean
// "call" node. Pattern mirrors treesitter_go_extractor.cpp.

#ifdef ICMG_HAS_TREESITTER_DART

#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include "../../core/logger.hpp"

#include <tree_sitter/api.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <memory>

extern "C" const TSLanguage *tree_sitter_dart(void);

namespace icmg::graph {

namespace {

static std::string fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

static std::string nodeText(TSNode n, const std::string& src) {
    uint32_t s = ts_node_start_byte(n);
    uint32_t e = ts_node_end_byte(n);
    if (e > src.size()) e = (uint32_t)src.size();
    if (s >= e) return {};
    return src.substr(s, e - s);
}

// Map a Dart declaration node type to a symbol kind (nullptr = not a symbol).
static const char* kindForType(const char* t) {
    if (!t) return nullptr;
    if (std::strcmp(t, "class_definition") == 0)      return "class";
    if (std::strcmp(t, "enum_declaration") == 0)      return "enum";
    if (std::strcmp(t, "mixin_declaration") == 0)     return "mixin";
    if (std::strcmp(t, "extension_declaration") == 0) return "extension";
    if (std::strcmp(t, "function_signature") == 0)    return "function";
    if (std::strcmp(t, "getter_signature") == 0)      return "getter";
    if (std::strcmp(t, "setter_signature") == 0)      return "setter";
    if (std::strcmp(t, "constructor_signature") == 0) return "constructor";
    return nullptr;
}

class DartExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty() || src.size() > 4 * 1024 * 1024) return out;

        TSParser* parser = ts_parser_new();
        if (!parser) return out;
        if (!ts_parser_set_language(parser, tree_sitter_dart())) {
            core::Logger::instance().warn("[treesitter-dart] ABI mismatch");
            ts_parser_delete(parser);
            return out;
        }
        TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                               src.c_str(), (uint32_t)src.size());
        if (!tree) { ts_parser_delete(parser); return out; }

        visit(ts_tree_root_node(tree), src, out);

        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return out;
    }

private:
    void visit(TSNode c, const std::string& src, std::vector<Symbol>& out) {
        const char* t = ts_node_type(c);
        const char* kind = kindForType(t);
        if (kind) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (!ts_node_is_null(id)) {
                Symbol s;
                s.kind = kind;
                s.name = nodeText(id, src);
                s.line_start = (int)ts_node_start_point(c).row + 1;
                s.line_end   = (int)ts_node_end_point(c).row + 1;
                s.body_hash  = fnv1a(nodeText(c, src));
                if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
            }
        }
        uint32_t n = ts_node_named_child_count(c);
        for (uint32_t i = 0; i < n; ++i) visit(ts_node_named_child(c, i), src, out);
    }
};

struct _TsDartReg {
    _TsDartReg() {
        auto factory = []() -> std::unique_ptr<BaseSymbolExtractor> {
            return std::make_unique<DartExtractor>();
        };
        auto& reg = core::Registry<BaseSymbolExtractor>::instance();
        reg.reg("ast-dart", factory);
        reg.reg("dart",     factory);
    }
} _ts_dart_inst;

} // namespace
} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER_DART

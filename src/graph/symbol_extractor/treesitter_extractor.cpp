// Phase 37 T2: tree-sitter AST extractor — real C/C++ impl.
//
// Compiled only when ICMG_HAS_TREESITTER is defined (CMake -DICMG_USE_TREESITTER=ON
// with libtree-sitter present + vendored tree-sitter-c grammar at
// third_party/tree-sitter-c/src/parser.c).
//
// Coverage: C/C++ (function_definition, struct_specifier, enum_specifier,
// type_definition). C++ class_specifier reuses C grammar's struct path — good
// enough for symbol indexing. Future: vendor tree-sitter-cpp / -python /
// -typescript for fuller coverage.
//
// Registers under "ast-c" + extension-keyed aliases. Factory picks ahead of
// regex extractor when filepath ends in .c/.h/.cpp/.hpp/.cc/.hh.

#ifdef ICMG_HAS_TREESITTER

#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include "../../core/logger.hpp"

#include <tree_sitter/api.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <memory>

extern "C" const TSLanguage *tree_sitter_c(void);

namespace icmg::graph {

namespace {

// FNV1a 64-bit hash → hex string. Same as elsewhere in the codebase.
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

// Find first descendant whose type matches one of the given types.
// Depth-limited to keep search cheap on deep declarators.
static TSNode findDescendantOfType(TSNode root, const char* type, int depth = 6) {
    if (depth < 0) return {0};
    uint32_t n = ts_node_child_count(root);
    for (uint32_t i = 0; i < n; ++i) {
        TSNode c = ts_node_child(root, i);
        const char* t = ts_node_type(c);
        if (t && std::strcmp(t, type) == 0) return c;
        TSNode r = findDescendantOfType(c, type, depth - 1);
        if (!ts_node_is_null(r)) return r;
    }
    return {0};
}

// Walk subtree collecting any `call_expression` -> function identifier names.
static void collectCalls(TSNode n, const std::string& src, std::vector<std::string>& out) {
    const char* t = ts_node_type(n);
    if (t && std::strcmp(t, "call_expression") == 0) {
        TSNode fn = ts_node_child_by_field_name(n, "function", 8);
        if (!ts_node_is_null(fn)) {
            // Function may be identifier or field_expression.
            TSNode id = fn;
            const char* ft = ts_node_type(fn);
            if (ft && (std::strcmp(ft, "field_expression") == 0
                    || std::strcmp(ft, "qualified_identifier") == 0)) {
                TSNode field = ts_node_child_by_field_name(fn, "field", 5);
                if (!ts_node_is_null(field)) id = field;
            }
            std::string name = nodeText(id, src);
            if (!name.empty() && name.size() < 128) out.push_back(name);
        }
    }
    uint32_t cn = ts_node_child_count(n);
    for (uint32_t i = 0; i < cn; ++i) collectCalls(ts_node_child(n, i), src, out);
}

class TreeSitterCExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty() || src.size() > 4 * 1024 * 1024) return out; // 4MB cap

        TSParser* parser = ts_parser_new();
        if (!parser) return out;
        if (!ts_parser_set_language(parser, tree_sitter_c())) {
            core::Logger::instance().warn(
                "tree-sitter: language ABI mismatch; skip extraction");
            ts_parser_delete(parser);
            return out;
        }
        TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                              src.c_str(), (uint32_t)src.size());
        if (!tree) { ts_parser_delete(parser); return out; }

        TSNode root = ts_tree_root_node(tree);
        uint32_t n = ts_node_named_child_count(root);
        for (uint32_t i = 0; i < n; ++i) {
            TSNode c = ts_node_named_child(root, i);
            const char* t = ts_node_type(c);
            if (!t) continue;

            Symbol s;
            s.line_start = (int)ts_node_start_point(c).row + 1;
            s.line_end   = (int)ts_node_end_point(c).row + 1;

            if (std::strcmp(t, "function_definition") == 0) {
                s.kind = "function";
                // Drill into declarator → function_declarator → identifier
                TSNode decl = ts_node_child_by_field_name(c, "declarator", 10);
                TSNode id = ts_node_is_null(decl)
                            ? findDescendantOfType(c, "identifier")
                            : findDescendantOfType(decl, "identifier");
                if (ts_node_is_null(id)) continue;
                s.name = nodeText(id, src);
                if (!ts_node_is_null(decl)) {
                    s.signature = nodeText(decl, src);
                    if (s.signature.size() > 200) s.signature.resize(200);
                }
                std::string body = nodeText(c, src);
                s.body_hash = fnv1a(body);
                collectCalls(c, src, s.calls);
            } else if (std::strcmp(t, "struct_specifier") == 0
                    || std::strcmp(t, "union_specifier") == 0
                    || std::strcmp(t, "enum_specifier") == 0
                    || std::strcmp(t, "class_specifier") == 0) {
                s.kind = (std::strcmp(t, "class_specifier") == 0) ? "class" : "struct";
                if (std::strcmp(t, "enum_specifier") == 0) s.kind = "enum";
                if (std::strcmp(t, "union_specifier") == 0) s.kind = "union";
                TSNode id = ts_node_child_by_field_name(c, "name", 4);
                if (ts_node_is_null(id)) continue;
                s.name = nodeText(id, src);
                s.body_hash = fnv1a(nodeText(c, src));
            } else if (std::strcmp(t, "type_definition") == 0) {
                s.kind = "typedef";
                // typedef has `declarator` field — the new type name. Avoid
                // grabbing the source type_identifier (e.g., `struct Point` part).
                TSNode decl = ts_node_child_by_field_name(c, "declarator", 10);
                TSNode id = ts_node_is_null(decl)
                            ? findDescendantOfType(c, "type_identifier")
                            : (std::strcmp(ts_node_type(decl), "type_identifier") == 0
                               ? decl
                               : findDescendantOfType(decl, "type_identifier"));
                if (ts_node_is_null(id)) continue;
                s.name = nodeText(id, src);
                s.body_hash = fnv1a(nodeText(c, src));
            } else {
                continue;
            }

            // Sanitize name.
            if (s.name.empty() || s.name.size() > 128) continue;
            out.push_back(std::move(s));
        }

        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return out;
    }
};

struct _TsCReg {
    _TsCReg() {
        auto factory = []() -> std::unique_ptr<BaseSymbolExtractor> {
            return std::make_unique<TreeSitterCExtractor>();
        };
        auto& reg = core::Registry<BaseSymbolExtractor>::instance();
        reg.reg("ast-c",   factory);
        reg.reg("ast-cpp", factory);  // C grammar is a serviceable C++ subset
        reg.reg("c",       factory);
        reg.reg("cpp",     factory);
        reg.reg("h",       factory);
        reg.reg("hpp",     factory);
    }
} _ts_c_inst;

} // namespace
} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER

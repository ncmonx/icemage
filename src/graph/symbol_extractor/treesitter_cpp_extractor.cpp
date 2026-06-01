// v1.55 Sub-D D3: tree-sitter C++ AST extractor.
//
// Compiled when ICMG_HAS_TREESITTER_CPP is defined. Parses:
//   function_definition, class_specifier, struct_specifier,
//   namespace_definition, call_expression.
// Pattern mirrors treesitter_python_extractor.cpp.

#ifdef ICMG_HAS_TREESITTER_CPP

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

extern "C" const TSLanguage *tree_sitter_cpp(void);

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

// Recursively find the leftmost identifier under a function_declarator chain.
// Handles: pointer_declarator, reference_declarator, function_declarator{declarator},
// field_identifier, identifier, qualified_identifier, destructor_name.
static std::string declName(TSNode n, const std::string& src) {
    if (ts_node_is_null(n)) return {};
    const char* t = ts_node_type(n);
    if (!t) return {};
    if (std::strcmp(t, "identifier") == 0
     || std::strcmp(t, "field_identifier") == 0
     || std::strcmp(t, "type_identifier") == 0
     || std::strcmp(t, "destructor_name") == 0
     || std::strcmp(t, "operator_name") == 0
     || std::strcmp(t, "qualified_identifier") == 0) {
        return nodeText(n, src);
    }
    // Try field "declarator" first (function_declarator, pointer/reference declarator).
    TSNode inner = ts_node_child_by_field_name(n, "declarator", 10);
    if (!ts_node_is_null(inner)) return declName(inner, src);
    // Fallback: walk children.
    uint32_t cn = ts_node_named_child_count(n);
    for (uint32_t i = 0; i < cn; ++i) {
        std::string r = declName(ts_node_named_child(n, i), src);
        if (!r.empty()) return r;
    }
    return {};
}

static void collectCalls(TSNode n, const std::string& src, std::vector<std::string>& out) {
    const char* t = ts_node_type(n);
    if (t && std::strcmp(t, "call_expression") == 0) {
        TSNode fn = ts_node_child_by_field_name(n, "function", 8);
        if (!ts_node_is_null(fn)) {
            TSNode id = fn;
            const char* ft = ts_node_type(fn);
            if (ft && std::strcmp(ft, "field_expression") == 0) {
                TSNode field = ts_node_child_by_field_name(fn, "field", 5);
                if (!ts_node_is_null(field)) id = field;
            } else if (ft && std::strcmp(ft, "qualified_identifier") == 0) {
                // foo::bar() -> take rightmost name
                TSNode name = ts_node_child_by_field_name(fn, "name", 4);
                if (!ts_node_is_null(name)) id = name;
            }
            std::string name = nodeText(id, src);
            if (!name.empty() && name.size() < 128) out.push_back(name);
        }
    }
    uint32_t cn = ts_node_child_count(n);
    for (uint32_t i = 0; i < cn; ++i) collectCalls(ts_node_child(n, i), src, out);
}

class CppExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty() || src.size() > 4 * 1024 * 1024) return out;

        TSParser* parser = ts_parser_new();
        if (!parser) return out;
        if (!ts_parser_set_language(parser, tree_sitter_cpp())) {
            core::Logger::instance().warn("[treesitter-cpp] ABI mismatch");
            ts_parser_delete(parser);
            return out;
        }
        TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                               src.c_str(), (uint32_t)src.size());
        if (!tree) { ts_parser_delete(parser); return out; }

        TSNode root = ts_tree_root_node(tree);
        uint32_t n = ts_node_named_child_count(root);
        for (uint32_t i = 0; i < n; ++i) {
            handle(ts_node_named_child(root, i), src, out, "");
        }

        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return out;
    }

private:
    void handle(TSNode c, const std::string& src, std::vector<Symbol>& out,
                 const std::string& class_prefix) {
        const char* t = ts_node_type(c);
        if (!t) return;

        if (std::strcmp(t, "function_definition") == 0) {
            TSNode decl = ts_node_child_by_field_name(c, "declarator", 10);
            std::string name = declName(decl, src);
            if (name.empty()) return;
            Symbol s;
            s.kind = class_prefix.empty() ? "function" : "method";
            s.name = class_prefix.empty() ? name : (class_prefix + "::" + name);
            s.line_start = (int)ts_node_start_point(c).row + 1;
            s.line_end   = (int)ts_node_end_point(c).row + 1;
            s.body_hash  = fnv1a(nodeText(c, src));
            collectCalls(c, src, s.calls);
            if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
        }
        else if (std::strcmp(t, "class_specifier") == 0
              || std::strcmp(t, "struct_specifier") == 0
              || std::strcmp(t, "union_specifier") == 0) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            std::string cname = nodeText(id, src);
            Symbol s;
            s.kind = (std::strcmp(t, "class_specifier") == 0)  ? "class"
                   : (std::strcmp(t, "struct_specifier") == 0) ? "struct"
                                                                : "union";
            s.name = class_prefix.empty() ? cname : (class_prefix + "::" + cname);
            s.line_start = (int)ts_node_start_point(c).row + 1;
            s.line_end   = (int)ts_node_end_point(c).row + 1;
            s.body_hash  = fnv1a(nodeText(c, src));
            // base_class_clause
            uint32_t cn = ts_node_named_child_count(c);
            for (uint32_t i = 0; i < cn; ++i) {
                TSNode ch = ts_node_named_child(c, i);
                const char* ct = ts_node_type(ch);
                if (ct && std::strcmp(ct, "base_class_clause") == 0) {
                    uint32_t bn = ts_node_named_child_count(ch);
                    for (uint32_t j = 0; j < bn; ++j) {
                        TSNode b = ts_node_named_child(ch, j);
                        std::string bs = nodeText(b, src);
                        if (!bs.empty() && bs.size() < 128) s.bases.push_back(bs);
                    }
                }
            }
            if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
            // Recurse into body for methods + nested types.
            TSNode body = ts_node_child_by_field_name(c, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t mn = ts_node_named_child_count(body);
                for (uint32_t i = 0; i < mn; ++i) {
                    handle(ts_node_named_child(body, i), src, out, s.name);
                }
            }
        }
        else if (std::strcmp(t, "namespace_definition") == 0) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            std::string ns = ts_node_is_null(id) ? "" : nodeText(id, src);
            std::string new_prefix = class_prefix.empty()
                                      ? ns
                                      : (ns.empty() ? class_prefix : class_prefix + "::" + ns);
            TSNode body = ts_node_child_by_field_name(c, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t mn = ts_node_named_child_count(body);
                for (uint32_t i = 0; i < mn; ++i) {
                    handle(ts_node_named_child(body, i), src, out, new_prefix);
                }
            }
        }
        else if (std::strcmp(t, "template_declaration") == 0) {
            // Unwrap: recurse into the templated decl.
            uint32_t cn = ts_node_named_child_count(c);
            for (uint32_t i = 0; i < cn; ++i) {
                handle(ts_node_named_child(c, i), src, out, class_prefix);
            }
        }
        else if (std::strcmp(t, "linkage_specification") == 0) {
            // extern "C" { ... } -> recurse body.
            TSNode body = ts_node_child_by_field_name(c, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t mn = ts_node_named_child_count(body);
                for (uint32_t i = 0; i < mn; ++i) {
                    handle(ts_node_named_child(body, i), src, out, class_prefix);
                }
            }
        }
    }
};

struct _TsCppReg {
    _TsCppReg() {
        auto factory = []() -> std::unique_ptr<BaseSymbolExtractor> {
            return std::make_unique<CppExtractor>();
        };
        auto& reg = core::Registry<BaseSymbolExtractor>::instance();
        reg.reg("ast-cpp", factory);
        reg.reg("cpp",     factory);
        reg.reg("cxx",     factory);
        reg.reg("cc",      factory);
        reg.reg("c++",     factory);
        reg.reg("hpp",     factory);
        reg.reg("h",       factory);
    }
} _ts_cpp_inst;

} // namespace
} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER_CPP

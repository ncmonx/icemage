// v1.55 Sub-D D3: tree-sitter Rust AST extractor.
//
// Compiled when ICMG_HAS_TREESITTER_RUST is defined. Parses:
//   function_item, impl_item(methods), struct_item, enum_item, trait_item,
//   call_expression. Pattern mirrors treesitter_python_extractor.cpp.

#ifdef ICMG_HAS_TREESITTER_RUST

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

extern "C" const TSLanguage *tree_sitter_rust(void);

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

static void collectCalls(TSNode n, const std::string& src, std::vector<std::string>& out) {
    const char* t = ts_node_type(n);
    if (t && std::strcmp(t, "call_expression") == 0) {
        TSNode fn = ts_node_child_by_field_name(n, "function", 8);
        if (!ts_node_is_null(fn)) {
            TSNode id = fn;
            const char* ft = ts_node_type(fn);
            // field_expression / scoped_identifier: take rightmost id
            if (ft && std::strcmp(ft, "field_expression") == 0) {
                TSNode field = ts_node_child_by_field_name(fn, "field", 5);
                if (!ts_node_is_null(field)) id = field;
            } else if (ft && std::strcmp(ft, "scoped_identifier") == 0) {
                TSNode name = ts_node_child_by_field_name(fn, "name", 4);
                if (!ts_node_is_null(name)) id = name;
            }
            std::string name = nodeText(id, src);
            if (!name.empty() && name.size() < 128) out.push_back(name);
        }
    }
    // method calls: foo.bar() -> call_expression { function: field_expression{value, field} }
    // already covered above.
    // macro invocations: macro_invocation { macro }
    if (t && std::strcmp(t, "macro_invocation") == 0) {
        TSNode m = ts_node_child_by_field_name(n, "macro", 5);
        if (!ts_node_is_null(m)) {
            std::string name = nodeText(m, src) + "!";
            if (name.size() < 128) out.push_back(name);
        }
    }
    uint32_t cn = ts_node_child_count(n);
    for (uint32_t i = 0; i < cn; ++i) collectCalls(ts_node_child(n, i), src, out);
}

class RustExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty() || src.size() > 4 * 1024 * 1024) return out;

        TSParser* parser = ts_parser_new();
        if (!parser) return out;
        if (!ts_parser_set_language(parser, tree_sitter_rust())) {
            core::Logger::instance().warn("[treesitter-rust] ABI mismatch");
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
                 const std::string& impl_prefix) {
        const char* t = ts_node_type(c);
        if (!t) return;

        if (std::strcmp(t, "function_item") == 0) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            Symbol s;
            s.kind = impl_prefix.empty() ? "function" : "method";
            s.name = impl_prefix.empty()
                     ? nodeText(id, src)
                     : impl_prefix + "." + nodeText(id, src);
            s.line_start = (int)ts_node_start_point(c).row + 1;
            s.line_end   = (int)ts_node_end_point(c).row + 1;
            s.body_hash  = fnv1a(nodeText(c, src));
            collectCalls(c, src, s.calls);
            if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
        }
        else if (std::strcmp(t, "impl_item") == 0) {
            TSNode tp = ts_node_child_by_field_name(c, "type", 4);
            std::string type_name = ts_node_is_null(tp) ? "" : nodeText(tp, src);
            // optional trait: trait field
            TSNode trait = ts_node_child_by_field_name(c, "trait", 5);
            std::string trait_name = ts_node_is_null(trait) ? "" : nodeText(trait, src);
            if (!trait_name.empty() && !type_name.empty()) {
                Symbol s;
                s.kind = "impl";
                s.name = type_name;
                s.line_start = (int)ts_node_start_point(c).row + 1;
                s.line_end   = (int)ts_node_end_point(c).row + 1;
                s.body_hash  = fnv1a(nodeText(c, src));
                s.bases.push_back(trait_name);
                if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
            }
            // Recurse into impl body for methods.
            TSNode body = ts_node_child_by_field_name(c, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t mn = ts_node_named_child_count(body);
                for (uint32_t i = 0; i < mn; ++i) {
                    handle(ts_node_named_child(body, i), src, out, type_name);
                }
            }
        }
        else if (std::strcmp(t, "struct_item") == 0
              || std::strcmp(t, "enum_item") == 0
              || std::strcmp(t, "trait_item") == 0
              || std::strcmp(t, "union_item") == 0) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            Symbol s;
            if      (std::strcmp(t, "struct_item") == 0) s.kind = "struct";
            else if (std::strcmp(t, "enum_item") == 0)   s.kind = "enum";
            else if (std::strcmp(t, "trait_item") == 0)  s.kind = "trait";
            else                                          s.kind = "union";
            s.name = nodeText(id, src);
            s.line_start = (int)ts_node_start_point(c).row + 1;
            s.line_end   = (int)ts_node_end_point(c).row + 1;
            s.body_hash  = fnv1a(nodeText(c, src));
            if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
        }
        else if (std::strcmp(t, "mod_item") == 0) {
            // Recurse into module body so nested fns/structs/etc. are captured.
            TSNode body = ts_node_child_by_field_name(c, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t mn = ts_node_named_child_count(body);
                for (uint32_t i = 0; i < mn; ++i) {
                    handle(ts_node_named_child(body, i), src, out, impl_prefix);
                }
            }
        }
    }
};

struct _TsRustReg {
    _TsRustReg() {
        auto factory = []() -> std::unique_ptr<BaseSymbolExtractor> {
            return std::make_unique<RustExtractor>();
        };
        auto& reg = core::Registry<BaseSymbolExtractor>::instance();
        reg.reg("ast-rust", factory);
        reg.reg("rust",     factory);
        reg.reg("rs",       factory);
    }
} _ts_rust_inst;

} // namespace
} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER_RUST

// Phase 40 T3: tree-sitter TypeScript AST extractor.
//
// Compiled when ICMG_HAS_TREESITTER_TS is defined (CMake auto-enables when
// vendored grammar present). Parses function/class/interface/type/enum at
// top level + method/property in class+interface bodies.

#ifdef ICMG_HAS_TREESITTER_TS

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

extern "C" const TSLanguage *tree_sitter_typescript(void);

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

static TSNode findChildByType(TSNode n, const char* type, int depth = 4) {
    if (depth < 0) return {0};
    uint32_t c = ts_node_child_count(n);
    for (uint32_t i = 0; i < c; ++i) {
        TSNode k = ts_node_child(n, i);
        const char* t = ts_node_type(k);
        if (t && std::strcmp(t, type) == 0) return k;
        TSNode r = findChildByType(k, type, depth - 1);
        if (!ts_node_is_null(r)) return r;
    }
    return {0};
}

static void collectCalls(TSNode n, const std::string& src, std::vector<std::string>& out) {
    const char* t = ts_node_type(n);
    if (t && std::strcmp(t, "call_expression") == 0) {
        TSNode fn = ts_node_child_by_field_name(n, "function", 8);
        if (!ts_node_is_null(fn)) {
            TSNode id = fn;
            const char* ft = ts_node_type(fn);
            if (ft && std::strcmp(ft, "member_expression") == 0) {
                TSNode prop = ts_node_child_by_field_name(fn, "property", 8);
                if (!ts_node_is_null(prop)) id = prop;
            }
            std::string name = nodeText(id, src);
            if (!name.empty() && name.size() < 128) out.push_back(name);
        }
    }
    uint32_t cn = ts_node_child_count(n);
    for (uint32_t i = 0; i < cn; ++i) collectCalls(ts_node_child(n, i), src, out);
}

class TypeScriptExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty() || src.size() > 4 * 1024 * 1024) return out;

        TSParser* parser = ts_parser_new();
        if (!parser) return out;
        if (!ts_parser_set_language(parser, tree_sitter_typescript())) {
            core::Logger::instance().warn("[treesitter-ts] ABI mismatch");
            ts_parser_delete(parser);
            return out;
        }
        TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                               src.c_str(), (uint32_t)src.size());
        if (!tree) { ts_parser_delete(parser); return out; }

        TSNode root = ts_tree_root_node(tree);
        emit(root, src, out);

        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return out;
    }

private:
    void emit(TSNode root, const std::string& src, std::vector<Symbol>& out) {
        uint32_t n = ts_node_named_child_count(root);
        for (uint32_t i = 0; i < n; ++i) {
            TSNode c = ts_node_named_child(root, i);
            handleNode(c, src, out, /*nested*/ false);
        }
    }

    void handleNode(TSNode c, const std::string& src,
                     std::vector<Symbol>& out, bool nested) {
        const char* t = ts_node_type(c);
        if (!t) return;
        Symbol s;
        s.line_start = (int)ts_node_start_point(c).row + 1;
        s.line_end   = (int)ts_node_end_point(c).row + 1;

        // function f(...) {...}
        if (std::strcmp(t, "function_declaration") == 0) {
            s.kind = "function";
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            s.name = nodeText(id, src);
            s.body_hash = fnv1a(nodeText(c, src));
            collectCalls(c, src, s.calls);
        }
        // class Foo {...}
        else if (std::strcmp(t, "class_declaration") == 0
              || std::strcmp(t, "abstract_class_declaration") == 0) {
            s.kind = "class";
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            s.name = nodeText(id, src);
            s.body_hash = fnv1a(nodeText(c, src));
            // Extract methods inside.
            TSNode body = ts_node_child_by_field_name(c, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t mn = ts_node_named_child_count(body);
                for (uint32_t i = 0; i < mn; ++i) {
                    TSNode m = ts_node_named_child(body, i);
                    const char* mt = ts_node_type(m);
                    if (!mt) continue;
                    if (std::strcmp(mt, "method_definition") == 0
                     || std::strcmp(mt, "method_signature") == 0) {
                        Symbol ms;
                        TSNode mid = ts_node_child_by_field_name(m, "name", 4);
                        if (ts_node_is_null(mid)) continue;
                        ms.kind = "method";
                        ms.name = s.name + "." + nodeText(mid, src);
                        ms.line_start = (int)ts_node_start_point(m).row + 1;
                        ms.line_end   = (int)ts_node_end_point(m).row + 1;
                        ms.body_hash = fnv1a(nodeText(m, src));
                        collectCalls(m, src, ms.calls);
                        out.push_back(std::move(ms));
                    }
                }
            }
        }
        // interface IX {...}
        else if (std::strcmp(t, "interface_declaration") == 0) {
            s.kind = "interface";
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            s.name = nodeText(id, src);
            s.body_hash = fnv1a(nodeText(c, src));
        }
        else if (std::strcmp(t, "type_alias_declaration") == 0) {
            s.kind = "type";
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            s.name = nodeText(id, src);
            s.body_hash = fnv1a(nodeText(c, src));
        }
        else if (std::strcmp(t, "enum_declaration") == 0) {
            s.kind = "enum";
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            s.name = nodeText(id, src);
            s.body_hash = fnv1a(nodeText(c, src));
        }
        // export {...} / export-default — descend.
        else if (std::strcmp(t, "export_statement") == 0) {
            uint32_t en = ts_node_named_child_count(c);
            for (uint32_t i = 0; i < en; ++i) {
                handleNode(ts_node_named_child(c, i), src, out, nested);
            }
            return;
        } else {
            return;
        }

        if (s.name.empty() || s.name.size() > 200) return;
        out.push_back(std::move(s));
        (void)nested;
    }
};

struct _TsTsReg {
    _TsTsReg() {
        auto factory = []() -> std::unique_ptr<BaseSymbolExtractor> {
            return std::make_unique<TypeScriptExtractor>();
        };
        auto& reg = core::Registry<BaseSymbolExtractor>::instance();
        reg.reg("ast-ts",  factory);
        reg.reg("ts",      factory);
        reg.reg("tsx",     factory);
        reg.reg("typescript", factory);
    }
} _ts_ts_inst;

} // namespace
} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER_TS

// Phase 40 T4: tree-sitter Python AST extractor.
//
// Compiled when ICMG_HAS_TREESITTER_PY is defined. Parses function_definition,
// class_definition, decorated_definition. Captures decorators in signature.

#ifdef ICMG_HAS_TREESITTER_PY

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

extern "C" const TSLanguage *tree_sitter_python(void);

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
    if (t && std::strcmp(t, "call") == 0) {
        TSNode fn = ts_node_child_by_field_name(n, "function", 8);
        if (!ts_node_is_null(fn)) {
            TSNode id = fn;
            const char* ft = ts_node_type(fn);
            if (ft && std::strcmp(ft, "attribute") == 0) {
                TSNode attr = ts_node_child_by_field_name(fn, "attribute", 9);
                if (!ts_node_is_null(attr)) id = attr;
            }
            std::string name = nodeText(id, src);
            if (!name.empty() && name.size() < 128) out.push_back(name);
        }
    }
    uint32_t cn = ts_node_child_count(n);
    for (uint32_t i = 0; i < cn; ++i) collectCalls(ts_node_child(n, i), src, out);
}

class PythonExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty() || src.size() > 4 * 1024 * 1024) return out;

        TSParser* parser = ts_parser_new();
        if (!parser) return out;
        if (!ts_parser_set_language(parser, tree_sitter_python())) {
            core::Logger::instance().warn("[treesitter-py] ABI mismatch");
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
            handle(c, src, out, /*class_prefix*/ "");
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

        // decorated_definition wraps function_definition or class_definition.
        if (std::strcmp(t, "decorated_definition") == 0) {
            TSNode def = ts_node_child_by_field_name(c, "definition", 10);
            if (!ts_node_is_null(def)) handle(def, src, out, class_prefix);
            return;
        }

        if (std::strcmp(t, "function_definition") == 0) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            Symbol s;
            s.kind = class_prefix.empty() ? "function" : "method";
            s.name = class_prefix.empty()
                     ? nodeText(id, src)
                     : class_prefix + "." + nodeText(id, src);
            s.line_start = (int)ts_node_start_point(c).row + 1;
            s.line_end   = (int)ts_node_end_point(c).row + 1;
            s.body_hash  = fnv1a(nodeText(c, src));
            collectCalls(c, src, s.calls);
            if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
        }
        else if (std::strcmp(t, "class_definition") == 0) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            std::string class_name = nodeText(id, src);
            Symbol s;
            s.kind = "class";
            s.name = class_name;
            s.line_start = (int)ts_node_start_point(c).row + 1;
            s.line_end   = (int)ts_node_end_point(c).row + 1;
            s.body_hash  = fnv1a(nodeText(c, src));
            // Capture base list.
            TSNode supers = ts_node_child_by_field_name(c, "superclasses", 12);
            if (!ts_node_is_null(supers)) {
                uint32_t bn = ts_node_named_child_count(supers);
                for (uint32_t i = 0; i < bn; ++i) {
                    TSNode b = ts_node_named_child(supers, i);
                    std::string bs = nodeText(b, src);
                    if (!bs.empty() && bs.size() < 128) s.bases.push_back(bs);
                }
            }
            if (!class_name.empty() && class_name.size() <= 200) out.push_back(std::move(s));
            // Recurse into class body for methods.
            TSNode body = ts_node_child_by_field_name(c, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t mn = ts_node_named_child_count(body);
                for (uint32_t i = 0; i < mn; ++i) {
                    handle(ts_node_named_child(body, i), src, out, class_name);
                }
            }
        }
    }
};

struct _TsPyReg {
    _TsPyReg() {
        auto factory = []() -> std::unique_ptr<BaseSymbolExtractor> {
            return std::make_unique<PythonExtractor>();
        };
        auto& reg = core::Registry<BaseSymbolExtractor>::instance();
        reg.reg("ast-py", factory);
        reg.reg("py",     factory);
        reg.reg("python", factory);
    }
} _ts_py_inst;

} // namespace
} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER_PY

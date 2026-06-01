// v1.55 Sub-D D3: tree-sitter Java AST extractor.
//
// Compiled when ICMG_HAS_TREESITTER_JAVA is defined. Parses:
//   class_declaration, interface_declaration, enum_declaration,
//   method_declaration, constructor_declaration, method_invocation.
// Pattern mirrors treesitter_python_extractor.cpp.

#ifdef ICMG_HAS_TREESITTER_JAVA

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

extern "C" const TSLanguage *tree_sitter_java(void);

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
    if (t && std::strcmp(t, "method_invocation") == 0) {
        TSNode id = ts_node_child_by_field_name(n, "name", 4);
        if (!ts_node_is_null(id)) {
            std::string name = nodeText(id, src);
            if (!name.empty() && name.size() < 128) out.push_back(name);
        }
    } else if (t && std::strcmp(t, "object_creation_expression") == 0) {
        TSNode tp = ts_node_child_by_field_name(n, "type", 4);
        if (!ts_node_is_null(tp)) {
            std::string name = nodeText(tp, src);
            if (!name.empty() && name.size() < 128) out.push_back("new " + name);
        }
    }
    uint32_t cn = ts_node_child_count(n);
    for (uint32_t i = 0; i < cn; ++i) collectCalls(ts_node_child(n, i), src, out);
}

class JavaExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty() || src.size() > 4 * 1024 * 1024) return out;

        TSParser* parser = ts_parser_new();
        if (!parser) return out;
        if (!ts_parser_set_language(parser, tree_sitter_java())) {
            core::Logger::instance().warn("[treesitter-java] ABI mismatch");
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

        bool is_class = std::strcmp(t, "class_declaration") == 0;
        bool is_iface = std::strcmp(t, "interface_declaration") == 0;
        bool is_enum  = std::strcmp(t, "enum_declaration") == 0;
        bool is_rec   = std::strcmp(t, "record_declaration") == 0;

        if (is_class || is_iface || is_enum || is_rec) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            std::string cname = nodeText(id, src);
            Symbol s;
            s.kind = is_class ? "class"
                   : is_iface ? "interface"
                   : is_enum  ? "enum"
                              : "record";
            s.name = class_prefix.empty() ? cname : (class_prefix + "." + cname);
            s.line_start = (int)ts_node_start_point(c).row + 1;
            s.line_end   = (int)ts_node_end_point(c).row + 1;
            s.body_hash  = fnv1a(nodeText(c, src));
            // superclass
            TSNode sup = ts_node_child_by_field_name(c, "superclass", 10);
            if (!ts_node_is_null(sup)) {
                std::string b = nodeText(sup, src);
                // strip "extends " prefix if present
                const std::string ex = "extends ";
                if (b.rfind(ex, 0) == 0) b = b.substr(ex.size());
                if (!b.empty() && b.size() < 128) s.bases.push_back(b);
            }
            // interfaces (class) / extends-list (interface)
            TSNode ifs = ts_node_child_by_field_name(c, "interfaces", 10);
            if (!ts_node_is_null(ifs)) {
                uint32_t bn = ts_node_named_child_count(ifs);
                for (uint32_t i = 0; i < bn; ++i) {
                    TSNode b = ts_node_named_child(ifs, i);
                    std::string bs = nodeText(b, src);
                    if (!bs.empty() && bs.size() < 128) s.bases.push_back(bs);
                }
            }
            if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));

            // Recurse into class body for methods + nested types.
            TSNode body = ts_node_child_by_field_name(c, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t mn = ts_node_named_child_count(body);
                for (uint32_t i = 0; i < mn; ++i) {
                    handle(ts_node_named_child(body, i), src, out, s.name);
                }
            }
        }
        else if (std::strcmp(t, "method_declaration") == 0
              || std::strcmp(t, "constructor_declaration") == 0) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            Symbol s;
            s.kind = (std::strcmp(t, "constructor_declaration") == 0) ? "constructor" : "method";
            std::string mname = nodeText(id, src);
            s.name = class_prefix.empty() ? mname : (class_prefix + "." + mname);
            s.line_start = (int)ts_node_start_point(c).row + 1;
            s.line_end   = (int)ts_node_end_point(c).row + 1;
            s.body_hash  = fnv1a(nodeText(c, src));
            collectCalls(c, src, s.calls);
            if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
        }
    }
};

struct _TsJavaReg {
    _TsJavaReg() {
        auto factory = []() -> std::unique_ptr<BaseSymbolExtractor> {
            return std::make_unique<JavaExtractor>();
        };
        auto& reg = core::Registry<BaseSymbolExtractor>::instance();
        reg.reg("ast-java", factory);
        reg.reg("java",     factory);
    }
} _ts_java_inst;

} // namespace
} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER_JAVA

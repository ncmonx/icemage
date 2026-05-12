// Phase 82 T3: tree-sitter Java symbol extractor.
// Extracts: class_declaration, interface_declaration, method_declaration,
// constructor_declaration, enum_declaration.

#ifdef ICMG_HAS_TREESITTER_JV

#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include <tree_sitter/api.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

extern "C" const TSLanguage *tree_sitter_java(void);

namespace icmg::graph {
namespace {

static std::string fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17]; std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

static std::string nodeText(TSNode n, const std::string& src) {
    uint32_t s = ts_node_start_byte(n), e = ts_node_end_byte(n);
    if (e > src.size()) e = (uint32_t)src.size();
    return s < e ? src.substr(s, e - s) : "";
}

static TSNode childByField(TSNode n, const char* field) {
    return ts_node_child_by_field_name(n, field, (uint32_t)strlen(field));
}

static void collectCalls(TSNode n, const std::string& src,
                         std::vector<std::string>& out, int depth = 8) {
    if (depth < 0) return;
    const char* t = ts_node_type(n);
    if (t && std::strcmp(t, "method_invocation") == 0) {
        TSNode name = childByField(n, "name");
        if (!ts_node_is_null(name)) {
            std::string nm = nodeText(name, src);
            if (!nm.empty()) out.push_back(nm);
        }
    }
    uint32_t cnt = ts_node_child_count(n);
    for (uint32_t i = 0; i < cnt; ++i)
        collectCalls(ts_node_child(n, i), src, out, depth - 1);
}

// Recursively extract methods from a class body node.
static void extractMethods(TSNode body, const std::string& src,
                            const std::string& class_name,
                            std::vector<Symbol>& result) {
    uint32_t cnt = ts_node_child_count(body);
    for (uint32_t i = 0; i < cnt; ++i) {
        TSNode item = ts_node_child(body, i);
        const char* t = ts_node_type(item);
        if (!t) continue;

        Symbol sym;
        sym.line_start = (int)ts_node_start_point(item).row + 1;
        sym.line_end   = (int)ts_node_end_point(item).row + 1;

        if (std::strcmp(t, "method_declaration") == 0) {
            TSNode name = childByField(item, "name");
            if (ts_node_is_null(name)) continue;
            sym.kind      = "method";
            sym.name      = class_name + "." + nodeText(name, src);
            sym.signature = sym.name + "()";
            sym.body_hash = fnv1a(nodeText(item, src));
            collectCalls(item, src, sym.calls);
            result.push_back(std::move(sym));

        } else if (std::strcmp(t, "constructor_declaration") == 0) {
            TSNode name = childByField(item, "name");
            if (ts_node_is_null(name)) continue;
            sym.kind      = "constructor";
            sym.name      = class_name + "." + nodeText(name, src);
            sym.body_hash = fnv1a(nodeText(item, src));
            collectCalls(item, src, sym.calls);
            result.push_back(std::move(sym));
        }
    }
}

class TreeSitterJavaExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                       const std::string& content) override {
        std::vector<Symbol> result;

        TSParser* parser = ts_parser_new();
        ts_parser_set_language(parser, tree_sitter_java());
        TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                              content.c_str(), (uint32_t)content.size());
        TSNode root = ts_tree_root_node(tree);

        // Walk top-level + one level deep (inside package/module declarations)
        std::function<void(TSNode)> walk = [&](TSNode node) {
            uint32_t cnt = ts_node_child_count(node);
            for (uint32_t i = 0; i < cnt; ++i) {
                TSNode child = ts_node_child(node, i);
                const char* type = ts_node_type(child);
                if (!type) continue;

                Symbol sym;
                sym.line_start = (int)ts_node_start_point(child).row + 1;
                sym.line_end   = (int)ts_node_end_point(child).row + 1;

                if (std::strcmp(type, "class_declaration") == 0) {
                    TSNode name = childByField(child, "name");
                    if (ts_node_is_null(name)) continue;
                    sym.kind      = "class";
                    sym.name      = nodeText(name, content);
                    sym.body_hash = fnv1a(sym.name);
                    result.push_back(sym);
                    TSNode body = childByField(child, "body");
                    if (!ts_node_is_null(body))
                        extractMethods(body, content, sym.name, result);

                } else if (std::strcmp(type, "interface_declaration") == 0) {
                    TSNode name = childByField(child, "name");
                    if (ts_node_is_null(name)) continue;
                    sym.kind      = "interface";
                    sym.name      = nodeText(name, content);
                    sym.body_hash = fnv1a(sym.name);
                    result.push_back(std::move(sym));

                } else if (std::strcmp(type, "enum_declaration") == 0) {
                    TSNode name = childByField(child, "name");
                    if (ts_node_is_null(name)) continue;
                    sym.kind      = "enum";
                    sym.name      = nodeText(name, content);
                    sym.body_hash = fnv1a(sym.name);
                    result.push_back(std::move(sym));

                } else {
                    // Recurse into package/import wrappers
                    walk(child);
                }
            }
        };

        walk(root);

        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return result;
    }
};

} // anon

static bool _reg_java = [] {
    auto& reg = core::Registry<BaseSymbolExtractor>::instance();
    auto factory = [] { return std::make_unique<TreeSitterJavaExtractor>(); };
    reg.reg("java", factory);
    return true;
}();

} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER_JV

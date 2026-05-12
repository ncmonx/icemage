// Phase 82 T3: tree-sitter Rust symbol extractor.
// Extracts: function_item, struct_item, impl_item (methods), trait_item, enum_item.

#ifdef ICMG_HAS_TREESITTER_RS

#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include <tree_sitter/api.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

extern "C" const TSLanguage *tree_sitter_rust(void);

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
    if (t && std::strcmp(t, "call_expression") == 0) {
        TSNode fn = childByField(n, "function");
        if (!ts_node_is_null(fn)) {
            // fn may be identifier or scoped_identifier (foo::bar)
            const char* ft = ts_node_type(fn);
            TSNode id = fn;
            if (ft && std::strcmp(ft, "scoped_identifier") == 0) {
                TSNode nm = childByField(fn, "name");
                if (!ts_node_is_null(nm)) id = nm;
            }
            std::string name = nodeText(id, src);
            if (!name.empty()) out.push_back(name);
        }
    }
    uint32_t cnt = ts_node_child_count(n);
    for (uint32_t i = 0; i < cnt; ++i)
        collectCalls(ts_node_child(n, i), src, out, depth - 1);
}

static void extractFromBlock(TSNode block, const std::string& src,
                              const std::string& parent_name,
                              std::vector<Symbol>& result) {
    uint32_t cnt = ts_node_child_count(block);
    for (uint32_t i = 0; i < cnt; ++i) {
        TSNode item = ts_node_child(block, i);
        const char* t = ts_node_type(item);
        if (!t || std::strcmp(t, "function_item") != 0) continue;
        TSNode name = childByField(item, "name");
        if (ts_node_is_null(name)) continue;
        Symbol sym;
        sym.kind       = "method";
        sym.name       = parent_name + "::" + nodeText(name, src);
        sym.line_start = (int)ts_node_start_point(item).row + 1;
        sym.line_end   = (int)ts_node_end_point(item).row + 1;
        sym.body_hash  = fnv1a(nodeText(item, src));
        collectCalls(item, src, sym.calls);
        result.push_back(std::move(sym));
    }
}

class TreeSitterRustExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                       const std::string& content) override {
        std::vector<Symbol> result;

        TSParser* parser = ts_parser_new();
        ts_parser_set_language(parser, tree_sitter_rust());
        TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                              content.c_str(), (uint32_t)content.size());
        TSNode root = ts_tree_root_node(tree);

        uint32_t count = ts_node_child_count(root);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_child(root, i);
            const char* type = ts_node_type(child);
            if (!type) continue;

            Symbol sym;
            sym.line_start = (int)ts_node_start_point(child).row + 1;
            sym.line_end   = (int)ts_node_end_point(child).row + 1;

            if (std::strcmp(type, "function_item") == 0) {
                TSNode name = childByField(child, "name");
                if (ts_node_is_null(name)) continue;
                sym.kind      = "function";
                sym.name      = nodeText(name, content);
                sym.body_hash = fnv1a(nodeText(child, content));
                collectCalls(child, content, sym.calls);

            } else if (std::strcmp(type, "struct_item") == 0) {
                TSNode name = childByField(child, "name");
                if (ts_node_is_null(name)) continue;
                sym.kind      = "struct";
                sym.name      = nodeText(name, content);
                sym.body_hash = fnv1a(nodeText(child, content));

            } else if (std::strcmp(type, "enum_item") == 0) {
                TSNode name = childByField(child, "name");
                if (ts_node_is_null(name)) continue;
                sym.kind      = "enum";
                sym.name      = nodeText(name, content);
                sym.body_hash = fnv1a(nodeText(child, content));

            } else if (std::strcmp(type, "trait_item") == 0) {
                TSNode name = childByField(child, "name");
                if (ts_node_is_null(name)) continue;
                sym.kind      = "trait";
                sym.name      = nodeText(name, content);
                sym.body_hash = fnv1a(nodeText(child, content));

            } else if (std::strcmp(type, "impl_item") == 0) {
                TSNode type_name = childByField(child, "type");
                std::string impl_name = !ts_node_is_null(type_name)
                                        ? nodeText(type_name, content) : "impl";
                sym.kind      = "impl";
                sym.name      = "impl " + impl_name;
                sym.body_hash = fnv1a(impl_name);
                // Extract methods inside impl block
                TSNode body = childByField(child, "body");
                if (!ts_node_is_null(body))
                    extractFromBlock(body, content, impl_name, result);

            } else {
                continue;
            }

            if (!sym.name.empty())
                result.push_back(std::move(sym));
        }

        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return result;
    }
};

} // anon

static bool _reg_rust = [] {
    auto& reg = core::Registry<BaseSymbolExtractor>::instance();
    auto factory = [] { return std::make_unique<TreeSitterRustExtractor>(); };
    reg.reg("rust", factory);
    return true;
}();

} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER_RS

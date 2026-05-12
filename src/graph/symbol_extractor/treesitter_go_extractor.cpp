// Phase 82 T3: tree-sitter Go symbol extractor.
// Extracts: function_declaration, method_declaration, type_declaration
// (struct, interface). Call edges via call_expression walk.
// Guard: compiled only when ICMG_HAS_TREESITTER_GO is defined.

#ifdef ICMG_HAS_TREESITTER_GO

#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include "../../core/logger.hpp"
#include <tree_sitter/api.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

extern "C" const TSLanguage *tree_sitter_go(void);

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
                         std::vector<std::string>& out) {
    const char* t = ts_node_type(n);
    if (t && std::strcmp(t, "call_expression") == 0) {
        TSNode fn = childByField(n, "function");
        if (!ts_node_is_null(fn)) {
            // fn may be identifier or selector_expression (pkg.Fn)
            const char* ft = ts_node_type(fn);
            TSNode id = fn;
            if (ft && std::strcmp(ft, "selector_expression") == 0) {
                TSNode sel = childByField(fn, "field");
                if (!ts_node_is_null(sel)) id = sel;
            }
            std::string name = nodeText(id, src);
            if (!name.empty() && std::islower((unsigned char)name[0]) == 0)
                out.push_back(name); // only exported (capital) calls
        }
    }
    uint32_t cnt = ts_node_child_count(n);
    for (uint32_t i = 0; i < cnt; ++i)
        collectCalls(ts_node_child(n, i), src, out);
}

class TreeSitterGoExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                       const std::string& content) override {
        std::vector<Symbol> result;

        TSParser* parser = ts_parser_new();
        ts_parser_set_language(parser, tree_sitter_go());
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

            if (std::strcmp(type, "function_declaration") == 0) {
                TSNode name = childByField(child, "name");
                if (ts_node_is_null(name)) continue;
                sym.kind = "function";
                sym.name = nodeText(name, content);
                TSNode params = childByField(child, "parameters");
                sym.signature = sym.name + (!ts_node_is_null(params)
                                            ? nodeText(params, content) : "()");
                std::string body = nodeText(child, content);
                sym.body_hash = fnv1a(body);
                collectCalls(child, content, sym.calls);

            } else if (std::strcmp(type, "method_declaration") == 0) {
                TSNode name = childByField(child, "name");
                if (ts_node_is_null(name)) continue;
                sym.kind = "method";
                sym.name = nodeText(name, content);
                TSNode recv = childByField(child, "receiver");
                std::string recv_str = !ts_node_is_null(recv)
                                       ? nodeText(recv, content) : "";
                sym.signature = recv_str + " " + sym.name + "()";
                sym.body_hash = fnv1a(nodeText(child, content));
                collectCalls(child, content, sym.calls);

            } else if (std::strcmp(type, "type_declaration") == 0) {
                // type Foo struct{} or type Bar interface{}
                uint32_t tc = ts_node_child_count(child);
                for (uint32_t j = 0; j < tc; ++j) {
                    TSNode spec = ts_node_child(child, j);
                    const char* st = ts_node_type(spec);
                    if (!st || std::strcmp(st, "type_spec") != 0) continue;
                    TSNode name = childByField(spec, "name");
                    TSNode tval = childByField(spec, "type");
                    if (ts_node_is_null(name)) continue;
                    const char* vtype = ts_node_is_null(tval) ? "" : ts_node_type(tval);
                    sym.name = nodeText(name, content);
                    if (vtype && std::strcmp(vtype, "struct_type") == 0)
                        sym.kind = "struct";
                    else if (vtype && std::strcmp(vtype, "interface_type") == 0)
                        sym.kind = "interface";
                    else
                        sym.kind = "type";
                    sym.signature = "type " + sym.name;
                    sym.body_hash = fnv1a(nodeText(spec, content));
                }
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

// Register under "go" — scanner.cpp calls getSymbolExtractor(lang).
static bool _reg_go = [] {
    auto& reg = core::Registry<BaseSymbolExtractor>::instance();
    auto factory = [] { return std::make_unique<TreeSitterGoExtractor>(); };
    reg.reg("go", factory);
    return true;
}();

} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER_GO

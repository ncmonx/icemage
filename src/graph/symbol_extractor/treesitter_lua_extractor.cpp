// tree-sitter Lua AST symbol extractor.
//
// Compiled when ICMG_HAS_TREESITTER_LUA is defined (CMake -DICMG_USE_TREESITTER=ON
// with vendored tree-sitter-lua grammar at third_party/tree-sitter-lua/src).
// Parses: function_declaration (name field may be identifier / dot_index_expression
// / method_index_expression, e.g. `M.foo` / `obj:method`), and collects
// function_call names. Pattern mirrors treesitter_go_extractor.cpp.

#ifdef ICMG_HAS_TREESITTER_LUA

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

extern "C" const TSLanguage *tree_sitter_lua(void);

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

// Walk subtree collecting function_call -> called-name text.
static void collectCalls(TSNode n, const std::string& src, std::vector<std::string>& out) {
    const char* t = ts_node_type(n);
    if (t && std::strcmp(t, "function_call") == 0) {
        TSNode nm = ts_node_child_by_field_name(n, "name", 4);
        if (!ts_node_is_null(nm)) {
            std::string c = nodeText(nm, src);
            if (!c.empty() && c.size() <= 200) out.push_back(std::move(c));
        }
    }
    uint32_t cn = ts_node_child_count(n);
    for (uint32_t i = 0; i < cn; ++i) collectCalls(ts_node_child(n, i), src, out);
}

class LuaExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty() || src.size() > 4 * 1024 * 1024) return out;

        TSParser* parser = ts_parser_new();
        if (!parser) return out;
        if (!ts_parser_set_language(parser, tree_sitter_lua())) {
            core::Logger::instance().warn("[treesitter-lua] ABI mismatch");
            ts_parser_delete(parser);
            return out;
        }
        TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                               src.c_str(), (uint32_t)src.size());
        if (!tree) { ts_parser_delete(parser); return out; }

        TSNode root = ts_tree_root_node(tree);
        visit(root, src, out);

        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return out;
    }

private:
    void visit(TSNode c, const std::string& src, std::vector<Symbol>& out) {
        const char* t = ts_node_type(c);
        if (t && std::strcmp(t, "function_declaration") == 0) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (!ts_node_is_null(id)) {
                Symbol s;
                s.kind = "function";
                s.name = nodeText(id, src);
                s.line_start = (int)ts_node_start_point(c).row + 1;
                s.line_end   = (int)ts_node_end_point(c).row + 1;
                s.body_hash  = fnv1a(nodeText(c, src));
                collectCalls(c, src, s.calls);
                if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
            }
        }
        uint32_t n = ts_node_named_child_count(c);
        for (uint32_t i = 0; i < n; ++i) visit(ts_node_named_child(c, i), src, out);
    }
};

struct _TsLuaReg {
    _TsLuaReg() {
        auto factory = []() -> std::unique_ptr<BaseSymbolExtractor> {
            return std::make_unique<LuaExtractor>();
        };
        auto& reg = core::Registry<BaseSymbolExtractor>::instance();
        reg.reg("ast-lua", factory);
        reg.reg("lua",     factory);
    }
} _ts_lua_inst;

} // namespace
} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER_LUA

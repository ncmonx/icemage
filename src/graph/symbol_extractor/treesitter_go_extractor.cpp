// v1.55 Sub-D D3: tree-sitter Go AST extractor.
//
// Compiled when ICMG_HAS_TREESITTER_GO is defined. Parses:
//   function_declaration, method_declaration, type_declaration(struct/interface),
//   call_expression. Pattern mirrors treesitter_python_extractor.cpp.

#ifdef ICMG_HAS_TREESITTER_GO

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

extern "C" const TSLanguage *tree_sitter_go(void);

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
            // selector_expression: field "field" is method/func name
            if (ft && std::strcmp(ft, "selector_expression") == 0) {
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

class GoExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty() || src.size() > 4 * 1024 * 1024) return out;

        TSParser* parser = ts_parser_new();
        if (!parser) return out;
        if (!ts_parser_set_language(parser, tree_sitter_go())) {
            core::Logger::instance().warn("[treesitter-go] ABI mismatch");
            ts_parser_delete(parser);
            return out;
        }
        TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                               src.c_str(), (uint32_t)src.size());
        if (!tree) { ts_parser_delete(parser); return out; }

        TSNode root = ts_tree_root_node(tree);
        uint32_t n = ts_node_named_child_count(root);
        for (uint32_t i = 0; i < n; ++i) {
            handle(ts_node_named_child(root, i), src, out);
        }

        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return out;
    }

private:
    void handle(TSNode c, const std::string& src, std::vector<Symbol>& out) {
        const char* t = ts_node_type(c);
        if (!t) return;

        if (std::strcmp(t, "function_declaration") == 0) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            Symbol s;
            s.kind = "function";
            s.name = nodeText(id, src);
            s.line_start = (int)ts_node_start_point(c).row + 1;
            s.line_end   = (int)ts_node_end_point(c).row + 1;
            s.body_hash  = fnv1a(nodeText(c, src));
            collectCalls(c, src, s.calls);
            if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
        }
        else if (std::strcmp(t, "method_declaration") == 0) {
            TSNode id = ts_node_child_by_field_name(c, "name", 4);
            if (ts_node_is_null(id)) return;
            // Receiver type prefix: (r *Foo) -> Foo
            std::string recv;
            TSNode rcv = ts_node_child_by_field_name(c, "receiver", 8);
            if (!ts_node_is_null(rcv)) {
                // walk receiver subtree for type_identifier
                uint32_t rn = ts_node_named_child_count(rcv);
                for (uint32_t i = 0; i < rn && recv.empty(); ++i) {
                    TSNode p = ts_node_named_child(rcv, i);
                    uint32_t pn = ts_node_named_child_count(p);
                    for (uint32_t j = 0; j < pn; ++j) {
                        TSNode pc = ts_node_named_child(p, j);
                        const char* pt = ts_node_type(pc);
                        if (pt && (std::strcmp(pt, "type_identifier") == 0
                                || std::strcmp(pt, "pointer_type") == 0)) {
                            // pointer_type wraps type_identifier
                            if (std::strcmp(pt, "pointer_type") == 0) {
                                uint32_t qn = ts_node_named_child_count(pc);
                                for (uint32_t k = 0; k < qn; ++k) {
                                    TSNode qc = ts_node_named_child(pc, k);
                                    const char* qt = ts_node_type(qc);
                                    if (qt && std::strcmp(qt, "type_identifier") == 0) {
                                        recv = nodeText(qc, src);
                                        break;
                                    }
                                }
                            } else {
                                recv = nodeText(pc, src);
                            }
                            break;
                        }
                    }
                }
            }
            Symbol s;
            s.kind = "method";
            std::string mname = nodeText(id, src);
            s.name = recv.empty() ? mname : (recv + "." + mname);
            s.line_start = (int)ts_node_start_point(c).row + 1;
            s.line_end   = (int)ts_node_end_point(c).row + 1;
            s.body_hash  = fnv1a(nodeText(c, src));
            collectCalls(c, src, s.calls);
            if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
        }
        else if (std::strcmp(t, "type_declaration") == 0) {
            // type_declaration -> type_spec(name=ident, type=struct_type/interface_type)
            uint32_t tn = ts_node_named_child_count(c);
            for (uint32_t i = 0; i < tn; ++i) {
                TSNode spec = ts_node_named_child(c, i);
                const char* st = ts_node_type(spec);
                if (!st || std::strcmp(st, "type_spec") != 0) continue;
                TSNode id = ts_node_child_by_field_name(spec, "name", 4);
                if (ts_node_is_null(id)) continue;
                TSNode tp = ts_node_child_by_field_name(spec, "type", 4);
                std::string kind = "type";
                if (!ts_node_is_null(tp)) {
                    const char* tpt = ts_node_type(tp);
                    if (tpt && std::strcmp(tpt, "struct_type") == 0)        kind = "struct";
                    else if (tpt && std::strcmp(tpt, "interface_type") == 0) kind = "interface";
                }
                Symbol s;
                s.kind = kind;
                s.name = nodeText(id, src);
                s.line_start = (int)ts_node_start_point(spec).row + 1;
                s.line_end   = (int)ts_node_end_point(spec).row + 1;
                s.body_hash  = fnv1a(nodeText(spec, src));
                if (!s.name.empty() && s.name.size() <= 200) out.push_back(std::move(s));
            }
        }
    }
};

struct _TsGoReg {
    _TsGoReg() {
        auto factory = []() -> std::unique_ptr<BaseSymbolExtractor> {
            return std::make_unique<GoExtractor>();
        };
        auto& reg = core::Registry<BaseSymbolExtractor>::instance();
        reg.reg("ast-go", factory);
        reg.reg("go",     factory);
    }
} _ts_go_inst;

} // namespace
} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER_GO

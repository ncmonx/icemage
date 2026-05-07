#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include <regex>
#include <cstdint>
#include <unordered_set>
#include <algorithm>

namespace icmg::graph {

static std::string fnv1a64s(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

static int lineOfS(const std::string& src, size_t off) {
    int line = 1;
    for (size_t i = 0; i < off && i < src.size(); ++i) if (src[i] == '\n') ++line;
    return line;
}

class SqlSymbolExtractor : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& src) override {
        std::vector<Symbol> out;
        if (src.empty()) return out;

        static const std::regex re_sp(
            R"(CREATE\s+(?:OR\s+ALTER\s+)?(?:PROC(?:EDURE)?|FUNCTION)\s+(?:\[?(\w+)\]?\.)?\[?(\w+)\]?)",
            std::regex::ECMAScript | std::regex::icase);

        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_sp);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            Symbol sym;
            sym.kind = "sp";
            sym.name = m[2].str();
            sym.signature = m[0].str();
            sym.line_start = lineOfS(src, m.position(0));

            size_t after = m.position(0) + m.length(0);
            std::regex re_go(R"(\bGO\b)", std::regex::icase);
            std::smatch gm;
            std::string rest = src.substr(after);
            size_t end = src.size();
            if (std::regex_search(rest, gm, re_go)) {
                end = after + gm.position(0);
            }
            sym.line_end = lineOfS(src, end - 1);
            std::string body = src.substr(after, end - after);
            sym.body_hash = fnv1a64s(body);

            std::unordered_set<std::string> seen;
            std::regex re_call(R"(\bEXEC(?:UTE)?\s+\[?(\w+)\]?)",
                               std::regex::ECMAScript | std::regex::icase);
            for (auto cit = std::sregex_iterator(body.begin(), body.end(), re_call);
                 cit != std::sregex_iterator(); ++cit) {
                std::string callee = (*cit)[1].str();
                if (seen.insert(callee).second) sym.calls.push_back(callee);
            }
            out.push_back(std::move(sym));
        }
        return out;
    }
};

ICMG_REGISTER_SYMBOL_EXTRACTOR("sql", SqlSymbolExtractor);
namespace { struct _AliasSql {
    _AliasSql() {
        icmg::core::Registry<icmg::graph::BaseSymbolExtractor>::instance().reg("mssql",
            []() -> std::unique_ptr<icmg::graph::BaseSymbolExtractor> {
                return std::make_unique<SqlSymbolExtractor>();
            });
    }
} _alias_sql_inst; }

} // namespace icmg::graph

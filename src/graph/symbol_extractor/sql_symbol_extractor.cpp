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

        // ---- Stored procedures / functions ------------------------------------
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

            // Detect outgoing references: EXEC <sp>, FROM/JOIN/UPDATE/INTO/DELETE FROM <table>.
            // All flow into sym.calls — edge resolver tags them as call: prefix.
            std::unordered_set<std::string> seen;
            auto addRef = [&](const std::string& name){
                if (name.empty()) return;
                std::string lo = name;
                for (auto& c : lo) c = (char)::tolower((unsigned char)c);
                // Skip SQL noise keywords accidentally captured.
                static const std::unordered_set<std::string> stop = {
                    "select","where","group","order","having","with","values",
                    "set","top","distinct","case","when","then","else","end",
                    "and","or","not","null","nolock","readuncommitted","into",
                    "as","on","using","cross","apply","outer","inner","left","right"
                };
                if (stop.count(lo)) return;
                if (seen.insert(name).second) sym.calls.push_back(name);
            };

            std::regex re_call(R"(\bEXEC(?:UTE)?\s+\[?(\w+)\]?)",
                               std::regex::ECMAScript | std::regex::icase);
            for (auto cit = std::sregex_iterator(body.begin(), body.end(), re_call);
                 cit != std::sregex_iterator(); ++cit) {
                addRef((*cit)[1].str());
            }
            // Tables touched by SP body — FROM, JOIN, UPDATE, INSERT INTO, DELETE FROM.
            // Captures schema-qualified [dbo].[Order] -> "Order" only.
            std::regex re_table(
                R"(\b(?:FROM|JOIN|UPDATE|INSERT\s+INTO|DELETE\s+FROM|MERGE(?:\s+INTO)?|TRUNCATE\s+TABLE|INTO)\s+(?:\[?\w+\]?\.)?\[?(\w+)\]?)",
                std::regex::ECMAScript | std::regex::icase);
            for (auto cit = std::sregex_iterator(body.begin(), body.end(), re_table);
                 cit != std::sregex_iterator(); ++cit) {
                addRef((*cit)[1].str());
            }
            out.push_back(std::move(sym));
        }

        // ---- Tables -----------------------------------------------------------
        // CREATE TABLE [schema].[Name] (cols...). Captures the table identifier
        // even when wrapped in brackets / schema-qualified. Body extends to the
        // matching closing paren or `GO`.
        static const std::regex re_tbl(
            R"(CREATE\s+TABLE\s+(?:\[?\w+\]?\.)?\[?(\w+)\]?\s*\()",
            std::regex::ECMAScript | std::regex::icase);
        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_tbl);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            Symbol sym;
            sym.kind = "table";
            sym.name = m[1].str();
            sym.signature = m[0].str();
            sym.line_start = lineOfS(src, m.position(0));

            // Body: from after the opening "(" until matching ")" at depth 0.
            size_t paren_open = m.position(0) + m.length(0) - 1;   // index of '('
            size_t i = paren_open + 1;
            int depth = 1;
            while (i < src.size() && depth > 0) {
                char c = src[i];
                if (c == '(') ++depth;
                else if (c == ')') --depth;
                ++i;
            }
            size_t end = (depth == 0) ? i : src.size();
            sym.line_end = lineOfS(src, end - 1);
            std::string body = src.substr(paren_open, end - paren_open);
            sym.body_hash = fnv1a64s(body);
            out.push_back(std::move(sym));
        }

        // ---- Views (CREATE VIEW <name> AS ...) --------------------------------
        static const std::regex re_view(
            R"(CREATE\s+(?:OR\s+ALTER\s+)?VIEW\s+(?:\[?\w+\]?\.)?\[?(\w+)\]?)",
            std::regex::ECMAScript | std::regex::icase);
        for (auto it = std::sregex_iterator(src.begin(), src.end(), re_view);
             it != std::sregex_iterator(); ++it) {
            const auto& m = *it;
            Symbol sym;
            sym.kind = "view";
            sym.name = m[1].str();
            sym.signature = m[0].str();
            sym.line_start = lineOfS(src, m.position(0));
            // Body to next GO or EOF.
            size_t after = m.position(0) + m.length(0);
            std::regex re_go(R"(\bGO\b)", std::regex::icase);
            std::smatch gm;
            std::string rest = src.substr(after);
            size_t end = src.size();
            if (std::regex_search(rest, gm, re_go)) end = after + gm.position(0);
            sym.line_end = lineOfS(src, end - 1);
            std::string body = src.substr(after, end - after);
            sym.body_hash = fnv1a64s(body);
            // Views reference tables — same heuristic as SP.
            std::unordered_set<std::string> seen;
            std::regex re_table(
                R"(\b(?:FROM|JOIN)\s+(?:\[?\w+\]?\.)?\[?(\w+)\]?)",
                std::regex::ECMAScript | std::regex::icase);
            for (auto cit = std::sregex_iterator(body.begin(), body.end(), re_table);
                 cit != std::sregex_iterator(); ++cit) {
                std::string nm = (*cit)[1].str();
                if (seen.insert(nm).second) sym.calls.push_back(nm);
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

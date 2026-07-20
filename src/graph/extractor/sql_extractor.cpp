#include "sql_extractor.hpp"
#include "../../core/registry.hpp"
#include <regex>
#include <algorithm>
#include <cctype>

namespace icmg::graph {

namespace {

// Strip SQL comments so keywords inside them are not matched. Handles line
// comments (-- ... EOL) and block comments (/* ... */). Preserves newlines
// count is not needed here (we only regex the result). Does not try to honor
// string literals containing "--"; good enough for schema DDL.
std::string stripComments(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    enum { CODE, LINE, BLOCK } st = CODE;
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        char n = (i + 1 < in.size()) ? in[i + 1] : '\0';
        switch (st) {
            case CODE:
                if (c == '-' && n == '-') { st = LINE; ++i; }
                else if (c == '/' && n == '*') { st = BLOCK; ++i; }
                else out += c;
                break;
            case LINE:
                if (c == '\n') { st = CODE; out += c; }
                break;
            case BLOCK:
                if (c == '*' && n == '/') { st = CODE; ++i; }
                break;
        }
    }
    return out;
}

// Normalize an identifier: strip surrounding quotes/backticks/brackets and a
// schema qualifier (schema.table -> table). Lowercases nothing (keeps case).
std::string cleanIdent(std::string s) {
    // trim whitespace
    auto a = s.find_first_not_of(" \t\r\n");
    auto b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    s = s.substr(a, b - a + 1);
    // strip matched quotes/backticks/brackets
    auto strip = [](std::string& x, char open, char close) {
        if (x.size() >= 2 && x.front() == open && x.back() == close)
            x = x.substr(1, x.size() - 2);
    };
    strip(s, '"', '"');
    strip(s, '`', '`');
    strip(s, '[', ']');
    strip(s, '\'', '\'');
    // schema qualifier: take the part after the last unquoted dot
    auto dot = s.find_last_of('.');
    if (dot != std::string::npos) s = s.substr(dot + 1);
    // re-strip in case the table part was quoted: schema."tbl"
    strip(s, '"', '"');
    strip(s, '`', '`');
    strip(s, '[', ']');
    return s;
}

void pushUnique(std::vector<std::string>& v, const std::string& s) {
    if (s.empty()) return;
    if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

} // namespace

ExtractResult SqlExtractor::extract(const std::string& /*path*/,
                                    const std::string& content) {
    ExtractResult res;
    if (content.empty()) return res;

    const std::string sql = stripComments(content);

    // An identifier can be quoted/backticked/bracketed and schema-qualified.
    // Capture it loosely, then clean it up.
    const std::string ID = R"(([`"\[]?[A-Za-z0-9_$."`\]]+))";

    try {
        // CREATE TABLE [IF NOT EXISTS] <name>
        std::regex re_table(
            R"(create\s+(?:temp(?:orary)?\s+)?table\s+(?:if\s+not\s+exists\s+)?)" + ID,
            std::regex::icase);
        // CREATE [OR REPLACE] VIEW|FUNCTION|PROCEDURE|TRIGGER <name>
        std::regex re_routine(
            R"(create\s+(?:or\s+replace\s+)?(?:materialized\s+)?(?:view|function|procedure|trigger)\s+(?:if\s+not\s+exists\s+)?)" + ID,
            std::regex::icase);
        // REFERENCES <table> (inline FK or explicit FOREIGN KEY ... REFERENCES)
        std::regex re_ref(R"(references\s+)" + ID, std::regex::icase);

        auto scan = [&](const std::regex& re, auto&& onMatch) {
            for (auto it = std::sregex_iterator(sql.begin(), sql.end(), re);
                 it != std::sregex_iterator(); ++it) {
                onMatch(cleanIdent((*it)[1].str()));
            }
        };

        scan(re_table,   [&](const std::string& n){ pushUnique(res.tables, n); });
        scan(re_routine, [&](const std::string& n){ pushUnique(res.functions, n); });
        scan(re_ref,     [&](const std::string& n){
            if (!n.empty()) pushUnique(res.imports, "references:" + n);
        });
    } catch (...) {
        // regex failure -> return whatever was collected; never throw.
    }

    return res;
}

ICMG_REGISTER_EXTRACTOR("sql", SqlExtractor);

} // namespace icmg::graph

#include "sql_parser.hpp"
#include <algorithm>
#include <regex>
#include <sstream>
#include <cctype>

namespace icmg::sp {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string SqlParser::toLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string SqlParser::trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------------------
// A6: State machine — strip string literals + comments to avoid false matches
// States: DEFAULT, IN_STRING ('), IN_DSTRING ("), IN_LINE_COMMENT, IN_BLOCK_COMMENT
// ---------------------------------------------------------------------------

std::string SqlParser::stripNoise(const std::string& sql) const {
    std::string out;
    out.reserve(sql.size());
    size_t i = 0, n = sql.size();

    enum State { DEFAULT, IN_STRING, IN_DSTRING, IN_LINE_COMMENT, IN_BLOCK_COMMENT };
    State state = DEFAULT;

    while (i < n) {
        char c  = sql[i];
        char c2 = (i + 1 < n) ? sql[i+1] : '\0';

        switch (state) {
        case DEFAULT:
            if (c == '\'' ) { state = IN_STRING;        out += ' '; ++i; break; }
            if (c == '"'  ) { state = IN_DSTRING;       out += ' '; ++i; break; }
            if (c == '-' && c2 == '-') {
                state = IN_LINE_COMMENT; out += ' '; i += 2; break;
            }
            if (c == '/' && c2 == '*') {
                state = IN_BLOCK_COMMENT; out += ' '; i += 2; break;
            }
            out += c; ++i; break;

        case IN_STRING:
            if (c == '\'' && c2 == '\'') { i += 2; break; } // escaped ''
            if (c == '\'') { state = DEFAULT; ++i; break; }
            ++i; break;  // skip string content

        case IN_DSTRING:
            if (c == '"' && c2 == '"') { i += 2; break; }
            if (c == '"') { state = DEFAULT; ++i; break; }
            ++i; break;

        case IN_LINE_COMMENT:
            if (c == '\n') { state = DEFAULT; out += '\n'; }
            ++i; break;

        case IN_BLOCK_COMMENT:
            if (c == '*' && c2 == '/') { state = DEFAULT; i += 2; break; }
            if (c == '\n') out += '\n'; // preserve line structure
            ++i; break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// detectDbType
// ---------------------------------------------------------------------------

std::string SqlParser::detectDbType(const std::string& sql) const {
    std::string lo = toLower(sql);
    if (lo.find("create or replace") != std::string::npos) return "postgresql";
    if (lo.find("$$") != std::string::npos)                return "postgresql";
    if (lo.find("delimiter") != std::string::npos)         return "mysql";
    if (lo.find("begin tran") != std::string::npos)        return "mssql";
    if (lo.find("go\n") != std::string::npos ||
        lo.find("go\r") != std::string::npos)              return "mssql";
    if (lo.find("@") != std::string::npos)                 return "mssql";
    return "mssql"; // default
}

// ---------------------------------------------------------------------------
// extractContext — first comment block / "-- Description:" line
// ---------------------------------------------------------------------------

std::string SqlParser::extractContext(const std::string& sql) const {
    // Check for "-- Description: ..." pattern first
    static const std::regex re_desc(R"re(--\s*[Dd]escription\s*:\s*(.+))re");
    std::smatch m;
    if (std::regex_search(sql, m, re_desc)) return trim(m[1].str());

    // First /* ... */ block
    auto b = sql.find("/*");
    if (b != std::string::npos) {
        auto e = sql.find("*/", b + 2);
        if (e != std::string::npos) {
            std::string block = sql.substr(b + 2, e - b - 2);
            std::istringstream ss(block);
            std::string line, result;
            while (std::getline(ss, line)) {
                size_t p = line.find_first_not_of(" \t*\r");
                if (p != std::string::npos) {
                    if (!result.empty()) result += " ";
                    result += trim(line.substr(p));
                }
            }
            return result;
        }
    }

    // First -- comment before CREATE
    std::string lo = toLower(sql);
    auto create_pos = lo.find("create");
    if (create_pos == std::string::npos) create_pos = sql.size();
    std::istringstream ss(sql.substr(0, create_pos));
    std::string line;
    while (std::getline(ss, line)) {
        size_t p = line.find("--");
        if (p != std::string::npos) return trim(line.substr(p + 2));
    }
    return "";
}

// ---------------------------------------------------------------------------
// extractParams
// ---------------------------------------------------------------------------

std::vector<SpParameter> SqlParser::extractParams(const std::string& sql,
                                                    const std::string& db_type) const {
    std::vector<SpParameter> params;

    if (db_type == "mssql") {
        static const std::regex re(
            R"re(@(\w+)\s+(\w+(?:\s*\(\s*\d+(?:\s*,\s*\d+)?\s*\))?)\s*(?:=\s*([^,\)\n]+))?)re");
        auto beg = std::sregex_iterator(sql.begin(), sql.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = beg; it != end; ++it) {
            SpParameter p;
            p.name        = "@" + (*it)[1].str();
            p.type        = trim((*it)[2].str());
            p.direction   = "IN";
            p.default_val = trim((*it)[3].str());
            params.push_back(p);
        }
    } else {
        static const std::regex re(
            R"re(\b(IN|OUT|INOUT)\s+(\w+)\s+(\w+(?:\s*\(\s*\d+(?:\s*,\s*\d+)?\s*\))?))re",
            std::regex_constants::icase);
        auto beg = std::sregex_iterator(sql.begin(), sql.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = beg; it != end; ++it) {
            SpParameter p;
            p.direction = (*it)[1].str();
            for (char& c : p.direction)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            p.name = (*it)[2].str();
            p.type = trim((*it)[3].str());
            params.push_back(p);
        }
    }
    return params;
}

// ---------------------------------------------------------------------------
// extractTables — from cleaned (noise-stripped) SQL
// ---------------------------------------------------------------------------

std::vector<std::string> SqlParser::extractTables(const std::string& clean_sql) const {
    static const std::vector<std::string> keywords = {
        "select","from","where","join","inner","outer","left","right","on","as",
        "and","or","not","in","exists","null","is","begin","end","declare",
        "set","if","else","return","exec","execute","call","procedure","function",
        "table","view","into","values","update","delete","insert","create","drop",
        "alter","with","having","group","order","by","distinct","top","limit",
        "offset","union","all","case","when","then","convert","cast",
        "count","sum","max","min","avg","char","varchar","nvarchar","int","bigint",
        "date","datetime","decimal","float","bit","money","text","ntext","image",
        "nolock","rowlock","tablock","go","use","database","schema","dbo","asc","desc"
    };

    static const std::regex re_table(
        R"re(\b(?:from|join|into|update)\s+(\w+))re");

    // Also handle DELETE FROM
    static const std::regex re_del(R"re(\bdelete\s+from\s+(\w+))re");

    std::vector<std::string> tables;
    std::string lo = toLower(clean_sql);

    auto addTable = [&](const std::string& tbl) {
        if (tbl.empty()) return;
        bool is_kw = std::find(keywords.begin(), keywords.end(), tbl) != keywords.end();
        if (is_kw) return;
        if (std::find(tables.begin(), tables.end(), tbl) == tables.end())
            tables.push_back(tbl);
    };

    auto beg1 = std::sregex_iterator(lo.begin(), lo.end(), re_table);
    for (auto it = beg1; it != std::sregex_iterator(); ++it) addTable((*it)[1].str());

    auto beg2 = std::sregex_iterator(lo.begin(), lo.end(), re_del);
    for (auto it = beg2; it != std::sregex_iterator(); ++it) addTable((*it)[1].str());

    return tables;
}

// ---------------------------------------------------------------------------
// extractSpCalls
// ---------------------------------------------------------------------------

std::vector<std::string> SqlParser::extractSpCalls(const std::string& clean_sql,
                                                     const std::string& db_type) const {
    std::vector<std::string> calls;
    std::string lo = toLower(clean_sql);

    if (db_type == "mssql") {
        static const std::regex re(R"re(\bexec(?:ute)?\s+(\w+))re");
        auto beg = std::sregex_iterator(lo.begin(), lo.end(), re);
        for (auto it = beg; it != std::sregex_iterator(); ++it) {
            std::string name = (*it)[1].str();
            if (std::find(calls.begin(), calls.end(), name) == calls.end())
                calls.push_back(name);
        }
    } else {
        static const std::regex re(R"re(\bcall\s+(\w+))re");
        auto beg = std::sregex_iterator(lo.begin(), lo.end(), re);
        for (auto it = beg; it != std::sregex_iterator(); ++it) {
            std::string name = (*it)[1].str();
            if (std::find(calls.begin(), calls.end(), name) == calls.end())
                calls.push_back(name);
        }
    }
    return calls;
}

// ---------------------------------------------------------------------------
// parse()
// ---------------------------------------------------------------------------

ParseResult SqlParser::parse(const std::string& sql,
                              const std::string& hint_db_type) const {
    ParseResult res;

    res.db_type = hint_db_type.empty() ? detectDbType(sql) : hint_db_type;
    res.context = extractContext(sql);

    // SP name: CREATE [OR REPLACE] [DEFINER=...] PROCEDURE <name>
    {
        static const std::regex re_name(
            R"re(\bCREATE\s+(?:OR\s+REPLACE\s+)?(?:DEFINER\s*=\s*\S+\s+)?PROCEDURE\s+`?(\w+)`?)re",
            std::regex_constants::icase);
        std::smatch m;
        if (std::regex_search(sql, m, re_name))
            res.sp_name = m[1].str();
    }

    std::string clean   = stripNoise(sql);
    res.parameters      = extractParams(sql, res.db_type);
    res.tables          = extractTables(clean);
    res.sp_calls        = extractSpCalls(clean, res.db_type);

    // Remove self-references from sp_calls
    if (!res.sp_name.empty()) {
        std::string lo_name = toLower(res.sp_name);
        res.sp_calls.erase(
            std::remove_if(res.sp_calls.begin(), res.sp_calls.end(),
                [&](const std::string& s) { return toLower(s) == lo_name; }),
            res.sp_calls.end());
    }

    return res;
}

} // namespace icmg::sp

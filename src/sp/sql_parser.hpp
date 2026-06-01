#pragma once
#include "stored_procedure.hpp"
#include <string>
#include <vector>

namespace icmg::sp {

struct ParseResult {
    std::string sp_name;
    std::string db_type;
    std::string context;              // first comment block / description line
    std::vector<SpParameter> parameters;
    std::vector<std::string> tables;
    std::vector<std::string> sp_calls;
};

// A6: State-machine SQL parser — handles string literals, line/block comments
// so that fake table/SP names inside strings are not extracted.
class SqlParser {
public:
    ParseResult parse(const std::string& sql,
                      const std::string& hint_db_type = "") const;

private:
    std::string detectDbType(const std::string& sql) const;
    std::string extractContext(const std::string& sql) const;

    std::vector<SpParameter> extractParams(const std::string& sql,
                                           const std::string& db_type) const;

    // Returns SQL with string literals + comments replaced by whitespace.
    std::string stripNoise(const std::string& sql) const;

    std::vector<std::string> extractTables(const std::string& clean_sql) const;
    std::vector<std::string> extractSpCalls(const std::string& clean_sql,
                                             const std::string& db_type) const;

    static std::string toLower(const std::string& s);
    static std::string trim(const std::string& s);
};

} // namespace icmg::sp

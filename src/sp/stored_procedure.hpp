#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace icmg::sp {

struct SpParameter {
    std::string name;
    std::string type;
    std::string direction;    // IN|OUT|INOUT
    std::string default_val;
};

struct StoredProcedure {
    int64_t     id            = 0;
    std::string name;
    std::string db_type;      // mssql|mysql|postgresql|oracle
    std::string database_name;
    std::string content;      // full SQL text
    std::string context;      // description / first comment block
    std::vector<SpParameter>  parameters;
    std::string return_type;
    std::vector<std::string>  tables_used;
    std::vector<std::string>  sp_dependencies;
    std::string scope_path;
    std::string tags;
    int         version       = 1;
    int64_t     created_at    = 0;
    int64_t     updated_at    = 0;
};

struct SpVersion {
    int64_t     id         = 0;
    int64_t     sp_id      = 0;
    int         version    = 0;
    std::string content;
    std::string change_note;
    int64_t     created_at = 0;
};

} // namespace icmg::sp

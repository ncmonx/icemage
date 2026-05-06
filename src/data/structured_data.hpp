#pragma once
#include <string>
#include <cstdint>

namespace icmg::data {

struct StructuredData {
    int64_t     id         = 0;
    std::string data_type;   // model|view|behavior|schema
    std::string name;
    std::string scope_path;
    std::string content;
    std::string version    = "1.0";
    std::string tags;
    int64_t     created_at = 0;
    int64_t     updated_at = 0;
};

struct DataVersion {
    int64_t     id         = 0;
    int64_t     data_id    = 0;
    std::string version;
    std::string content;
    std::string change_note;
    int64_t     created_at = 0;
};

} // namespace icmg::data

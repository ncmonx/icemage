#pragma once
#include <string>
#include <cstdint>

namespace icmg::abbreviation {

struct Abbreviation {
    int64_t     id          = 0;
    std::string short_form;   // e.g. "bkm"
    std::string full_form;    // e.g. "bukti kas masuk"
    std::string domain;       // e.g. "accounting", "general", ""
    std::string scope_path;   // optional path prefix
    int         frequency   = 0;
    int64_t     created_at  = 0;
};

} // namespace icmg::abbreviation

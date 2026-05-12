#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace icmg::core {

struct ContextNode {
    int64_t     id          = 0;
    std::string node_key;       // unique slug, e.g. "project-overview"
    std::string title;
    std::string content;
    std::string source_file;    // origin path (CLAUDE.md / skill file)
    std::string tier;           // "hot" | "cold" | "skill"
    std::string tags;           // JSON array string, e.g. ["build","cmake"]
    bool        active      = true;
    int64_t     created_at  = 0;
    int64_t     updated_at  = 0;
};

} // namespace icmg::core

#pragma once
// v1.70.0 (#176): serialize recall results as JSON with GUARANTEED valid UTF-8.
//
// The old printJson emitted memory content with a hand-rolled escaper that
// passed raw bytes through. Memory rows can hold invalid UTF-8 (captured binary,
// e.g. a PNG 0x89 byte), so `icmg recall --json` produced output that threw
// `type_error.316` the moment any downstream consumer did json::dump() on it.
// Building via nlohmann + core::safeDump (error_handler::replace) substitutes
// U+FFFD for invalid sequences, so the emitted JSON is always valid UTF-8.
#include "../imem/memory_node.hpp"
#include "../core/json_safe.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace icmg::cli {

inline std::string recallNodesToJson(const std::vector<imem::MemoryNode>& nodes) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& n : nodes) {
        arr.push_back({
            {"id",         n.id},
            {"topic",      n.topic},
            {"content",    n.content},
            {"keywords",   n.keywords},
            {"importance", n.importance},
            {"frequency",  n.frequency},
            {"score",      n.score},
            {"git_sha",    n.git_sha},
        });
    }
    return icmg::core::safeDump(arr);
}

} // namespace icmg::cli

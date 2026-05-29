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

// ram-brain: full-fidelity (de)serialization for the RecallCache. Unlike the
// public recallNodesToJson (8 fields for --json), this round-trips every
// persisted field so a cache hit is indistinguishable from a fresh recall.
inline std::string cacheNodesToJson(const std::vector<imem::MemoryNode>& nodes) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& n : nodes) {
        arr.push_back({
            {"id", n.id}, {"topic", n.topic}, {"content", n.content},
            {"keywords", n.keywords}, {"importance", n.importance},
            {"frequency", n.frequency}, {"last_used", n.last_used},
            {"created_at", n.created_at}, {"expires_at", n.expires_at},
            {"deleted_at", n.deleted_at}, {"zone", n.zone}, {"pinned", n.pinned},
            {"git_sha", n.git_sha}, {"score", n.score},
        });
    }
    return icmg::core::safeDump(arr);
}

inline std::vector<imem::MemoryNode> cacheNodesFromJson(const std::string& s) {
    std::vector<imem::MemoryNode> out;
    try {
        auto arr = nlohmann::json::parse(s);
        if (!arr.is_array()) return out;
        for (const auto& j : arr) {
            imem::MemoryNode n;
            n.id         = j.value("id", (int64_t)0);
            n.topic      = j.value("topic", std::string());
            n.content    = j.value("content", std::string());
            n.keywords   = j.value("keywords", std::string());
            n.importance = j.value("importance", 1);
            n.frequency  = j.value("frequency", 1);
            n.last_used  = j.value("last_used", (int64_t)0);
            n.created_at = j.value("created_at", (int64_t)0);
            n.expires_at = j.value("expires_at", (int64_t)0);
            n.deleted_at = j.value("deleted_at", (int64_t)0);
            n.zone       = j.value("zone", std::string("default"));
            n.pinned     = j.value("pinned", 0);
            n.git_sha    = j.value("git_sha", std::string());
            n.score      = j.value("score", 0.0);
            out.push_back(std::move(n));
        }
    } catch (...) {}   // parse failure -> empty -> caller treats as miss
    return out;
}

} // namespace icmg::cli

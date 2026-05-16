#pragma once
// v1.3.0 Task 7: Shared skill chunk recall interface.
//
// Exposes recallSkillChunks() so both skill_cmd.cpp (doAsk) and
// core/hooks/internals.cpp (runUserPromptSkillSuggest) share the same
// BM25+cosine scoring logic without duplication.

#include "../../core/db.hpp"
#include <string>
#include <vector>

namespace icmg::cli {

struct ScoredChunk {
    std::string parent_path;
    std::string heading;
    std::string content;
    std::string skill_key;
    double      score = 0.0;
};

// Hybrid BM25+cosine recall over skill_chunks for the given query.
// top_n: max results returned. alpha: blend weight (0=pure BM25, 1=pure cosine).
// skill_key_filter: restrict to chunks of a single skill (empty = all skills).
// Returns results sorted descending by score. Fail-soft: errors → empty vector.
std::vector<ScoredChunk> recallSkillChunks(
    icmg::core::Db&    db,
    const std::string& query,
    int                top_n             = 5,
    double             alpha             = 0.5,
    const std::string& skill_key_filter  = "");

} // namespace icmg::cli

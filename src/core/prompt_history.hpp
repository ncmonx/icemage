#pragma once
// Zoned prompt->response history in the persona DB. Exact recall via normalized prompt key
// (case/space-insensitive), plus lexical find-similar (LIKE on prompt terms; FTS5 optional).
// Lets a repeated/similar prompt reuse the past solution instead of re-deriving it.
#include "db.hpp"
#include <string>
#include <utility>
#include <vector>

namespace icmg::core {

struct QARow { std::string zone, prompt, response; long long created_at = 0; };

// Word-set Jaccard similarity over the two prompts' significant tokens (slug split
// on '-', tokens len>=3). 1.0 = same token set, 0.0 = disjoint. Order-independent,
// deterministic, model-free. Used to gate active prompt-history reuse.
double promptJaccard(const std::string& a, const std::string& b);

// Result of a gated reuse lookup: the best stored Q/A whose prompt scores >= the
// requested minimum, or found=false when nothing clears the gate.
struct Suggestion { QARow row; double score = 0.0; bool found = false; };

class PromptHistory {
public:
    explicit PromptHistory(Db& db);
    void record(const std::string& user, const std::string& zone,
                const std::string& prompt, const std::string& response);
    bool recallExact(const std::string& user, const std::string& zone,
                     const std::string& prompt, std::string& response_out);
    std::vector<QARow> findSimilar(const std::string& user, const std::string& prompt, int limit);
    // Active reuse: scan up to `scan` lexically-similar candidates, score each by
    // promptJaccard against `prompt`, and return the best if it clears `minScore`.
    Suggestion suggest(const std::string& user, const std::string& prompt,
                       double minScore, int scan = 25);
    // List stored prompts for a user; empty zone = all zones. Newest first.
    std::vector<QARow> listZone(const std::string& user, const std::string& zone, int limit = 100);
    // Delete a stored prompt (matched by normalized prompt key within a zone).
    void forget(const std::string& user, const std::string& zone, const std::string& prompt);
    // Distinct zones with prompt counts for a user, busiest first.
    std::vector<std::pair<std::string,int>> zoneCounts(const std::string& user);
private:
    Db& db_;
    void ensure();
};

}  // namespace icmg::core

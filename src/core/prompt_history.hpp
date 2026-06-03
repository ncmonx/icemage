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

class PromptHistory {
public:
    explicit PromptHistory(Db& db);
    void record(const std::string& user, const std::string& zone,
                const std::string& prompt, const std::string& response);
    bool recallExact(const std::string& user, const std::string& zone,
                     const std::string& prompt, std::string& response_out);
    std::vector<QARow> findSimilar(const std::string& user, const std::string& prompt, int limit);
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

#pragma once
// Zoned prompt->response history in the persona DB. Exact recall via normalized prompt key
// (case/space-insensitive), plus lexical find-similar (LIKE on prompt terms; FTS5 optional).
// Lets a repeated/similar prompt reuse the past solution instead of re-deriving it.
#include "db.hpp"
#include <string>
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
private:
    Db& db_;
    void ensure();
};

}  // namespace icmg::core

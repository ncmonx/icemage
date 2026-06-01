// Phase 54 T2: testable Anthropic Batch API spec builder.
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace icmg::cli {

enum class BatchDirective { None, NoThink, Concise, Sayless };

struct BatchOpts {
    std::string model        = "claude-sonnet-4-5";
    int         max_tokens   = 2000;
    std::string id_prefix    = "task";
    BatchDirective directive = BatchDirective::None;
};

// Builds a Batch-API-shaped JSON object: {"requests":[{"custom_id":...,"params":{...}}, ...]}.
nlohmann::json buildBatchSpec(const std::vector<std::string>& tasks,
                               const BatchOpts& opts = {});

} // namespace icmg::cli

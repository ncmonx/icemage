#include "batch_builder.hpp"

namespace icmg::cli {

namespace {
const char* directivePreamble(BatchDirective d) {
    switch (d) {
        case BatchDirective::Sayless:
            return "Sayless mode ultra. Drop articles/filler. Fragments OK. "
                   "Arrows for causality. Reply under 60 words.\n\n";
        case BatchDirective::Concise:
            return "Answer directly. No analysis. Reply under 100 words. "
                   "No code blocks unless requested.\n\n";
        case BatchDirective::NoThink:
            return "Answer directly without analysis. Single-pass response.\n\n";
        case BatchDirective::None:
        default:
            return "";
    }
}
} // namespace

nlohmann::json buildBatchSpec(const std::vector<std::string>& tasks,
                               const BatchOpts& opts) {
    nlohmann::json out;
    out["requests"] = nlohmann::json::array();
    std::string preamble = directivePreamble(opts.directive);
    int idx = 1;
    for (auto& t : tasks) {
        nlohmann::json req;
        req["custom_id"] = opts.id_prefix + "-" + std::to_string(idx++);
        req["params"]["model"] = opts.model;
        req["params"]["max_tokens"] = opts.max_tokens;
        req["params"]["messages"] = nlohmann::json::array();
        nlohmann::json m;
        m["role"] = "user";
        m["content"] = preamble + t;
        req["params"]["messages"].push_back(m);
        out["requests"].push_back(req);
    }
    return out;
}

} // namespace icmg::cli

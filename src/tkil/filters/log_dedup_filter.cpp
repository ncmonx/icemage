// v1.20.4 (F6): log dedup filter — collapses repeated lines for docker logs
// and kubectl logs output. Matches command patterns like:
//   docker logs <container>
//   docker compose logs ...
//   kubectl logs <pod>
//   journalctl ...
//
// Strategy:
//   - Strip timestamp prefix when present (ISO-8601 / journalctl style)
//   - Collapse N consecutive identical (post-timestamp) lines → "<line> [xN]"
//   - Then apply head/tail truncation like default filter
//
// Net: 70-90% cut on noisy looped containers.

#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>

namespace icmg::tkil {

class LogDedupFilter : public BaseFilter {
public:
    std::string name() const override { return "log-dedup"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto raw_lines = splitLines(raw);
        res.original_lines = (int)raw_lines.size();

        // Strip ISO-8601 / Docker / journalctl timestamp prefix for comparison;
        // keep original line for output. Patterns covered:
        //   2026-05-20T12:34:56.789Z  <msg>
        //   2026-05-20 12:34:56       <msg>
        //   May 20 12:34:56 hostname  <msg>
        static const std::regex ts_re(
            R"(^\s*((\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:?\d{2})?\s+)|([A-Z][a-z]{2}\s+\d{1,2}\s+\d{2}:\d{2}:\d{2}\s+\S+\s+)))");

        auto strip_ts = [&](const std::string& l) -> std::string {
            return std::regex_replace(l, ts_re, "");
        };

        // Collapse consecutive identical lines (after stripping timestamp).
        std::vector<std::string> dedup;
        dedup.reserve(raw_lines.size());
        std::string prev_key;
        int run = 0;
        std::string prev_full;
        for (auto& l : raw_lines) {
            std::string key = strip_ts(l);
            if (key == prev_key && !key.empty()) {
                ++run;
                prev_full = l;
            } else {
                if (run > 1) {
                    if (!dedup.empty())
                        dedup.back() += "  [x" + std::to_string(run) + "]";
                }
                dedup.push_back(l);
                prev_key = key;
                run = 1;
                prev_full = l;
            }
        }
        if (run > 1 && !dedup.empty())
            dedup.back() += "  [x" + std::to_string(run) + "]";

        auto lines = collapseBlankRuns(dedup);

        constexpr int HEAD = 50;
        constexpr int TAIL = 20;
        if ((int)lines.size() <= HEAD + TAIL) {
            for (auto& l : lines) res.output += l + "\n";
            res.filtered_lines = (int)lines.size();
            return res;
        }
        for (int i = 0; i < HEAD; ++i) res.output += lines[i] + "\n";
        int omitted = (int)lines.size() - HEAD - TAIL;
        res.output += "\n... (" + std::to_string(omitted) + " lines omitted; deduped) ...\n\n";
        for (int i = (int)lines.size() - TAIL; i < (int)lines.size(); ++i)
            res.output += lines[i] + "\n";

        res.filtered_lines = HEAD + TAIL;
        res.was_truncated  = true;
        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("log-dedup", LogDedupFilter);

} // namespace icmg::tkil

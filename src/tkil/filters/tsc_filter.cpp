// TypeScript compiler (tsc) filter.
// tsc emits `<file>(<line>,<col>): error TS<code>: <msg>` lines + summary
// "Found N errors". Keep error lines + summary; drop noise (incremental
// progress, watcher messages, "Starting compilation"...).
#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>

namespace icmg::tkil {

class TscFilter : public BaseFilter {
public:
    std::string name() const override { return "tsc"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        std::regex re_err(R"(\berror TS\d+|\bwarning TS\d+|^Found \d+ error|^Compilation complete|^\s+\d+\s+\|)",
                          std::regex::ECMAScript);
        std::regex re_skip(R"(Starting compilation|File change detected|Watching for file changes|^$)",
                           std::regex::ECMAScript);

        std::vector<std::string> kept;
        for (auto& l : lines) {
            if (std::regex_search(l, re_err)) { kept.push_back(l); continue; }
            if (std::regex_search(l, re_skip)) continue;
            // Pretty errors: file path + arrow line under error
            if (l.find("→") != std::string::npos || l.find("~~~") != std::string::npos) {
                kept.push_back(l); continue;
            }
        }
        // Always keep last line (summary).
        if (!lines.empty() && (kept.empty() || kept.back() != lines.back())) {
            kept.push_back(lines.back());
        }
        for (auto& l : kept) res.output += l + "\n";
        res.filtered_lines = (int)kept.size();
        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("tsc", TscFilter);

} // namespace icmg::tkil

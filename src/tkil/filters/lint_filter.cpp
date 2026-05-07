// Lint output filter — eslint, clippy, ruff, golangci-lint, dotnet format.
// Strategy: keep error/warning lines + file:line:col + message; drop file
// summary noise, "✓" lines, configuration headers.
#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>

namespace icmg::tkil {

class LintFilter : public BaseFilter {
public:
    std::string name() const override { return "lint"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        // file:line:col indicators across linters.
        std::regex re_pos(R"((^|\s)(/|[A-Za-z]:|\.{1,2}/)[^\s:]+:\d+(:\d+)?)",
                          std::regex::ECMAScript);
        std::regex re_kw(R"(\b(error|warning|warn|note|help|fail|FAIL|panic|denied)\b)",
                         std::regex::ECMAScript | std::regex::icase);
        std::regex re_summary(R"(\d+\s+(error|warning|problem|issue|fail))",
                              std::regex::ECMAScript | std::regex::icase);
        std::regex re_skip(R"(^\s*✓|all checks passed|^\s*$|Checking\s|Linting\s)",
                           std::regex::ECMAScript | std::regex::icase);

        std::vector<std::string> kept;
        for (auto& l : lines) {
            if (std::regex_search(l, re_skip)) continue;
            if (std::regex_search(l, re_pos)  ||
                std::regex_search(l, re_kw)   ||
                std::regex_search(l, re_summary)) {
                kept.push_back(l);
            }
        }
        if (kept.empty() && !lines.empty()) kept.push_back(lines.back());
        for (auto& l : kept) res.output += l + "\n";
        res.filtered_lines = (int)kept.size();
        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("lint", LintFilter);

} // namespace icmg::tkil

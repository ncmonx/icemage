// Playwright test runner filter.
// Default reporter prints test names with ✘/✓ markers + screenshot paths on
// failure + retry info. Keep failures + screenshot paths + summary.
#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>

namespace icmg::tkil {

class PlaywrightFilter : public BaseFilter {
public:
    std::string name() const override { return "playwright"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        std::vector<std::string> kept;
        std::regex re_fail(R"(✘|×|failed|FAIL|TimeoutError|Error:|expect\(|toHaveText|toBeVisible|page\.goto)",
                           std::regex::ECMAScript | std::regex::icase);
        std::regex re_artifact(R"(\.(png|webm|zip|trace|html)\s*$)",
                               std::regex::ECMAScript);
        std::regex re_summary(R"(passed|failed|flaky|skipped|tests?\s)",
                              std::regex::ECMAScript | std::regex::icase);

        bool in_block = false;
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];
            // Failure markers
            if (l.find("✘") != std::string::npos ||
                l.find("FAIL") != std::string::npos ||
                l.find("Error:") != std::string::npos) {
                in_block = true; kept.push_back(l); continue;
            }
            // Artifact paths (screenshot/video) — always keep
            if (std::regex_search(l, re_artifact)) { kept.push_back(l); continue; }
            // Summary footer
            if (i >= lines.size() - 5 && std::regex_search(l, re_summary)) {
                kept.push_back(l); continue;
            }
            // Stack/expect lines while in block
            if (in_block) {
                if (l.empty()) { in_block = false; kept.push_back(l); continue; }
                if (std::regex_search(l, re_fail)) { kept.push_back(l); continue; }
                kept.push_back(l);
            }
        }
        if (kept.empty() && !lines.empty()) {
            size_t s = lines.size() > 3 ? lines.size() - 3 : 0;
            for (size_t i = s; i < lines.size(); ++i) kept.push_back(lines[i]);
        }
        for (auto& l : kept) res.output += l + "\n";
        res.filtered_lines = (int)kept.size();
        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("playwright", PlaywrightFilter);

} // namespace icmg::tkil

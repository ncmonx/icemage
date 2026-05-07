// Vitest reporter output filter.
// Dedicated handling: vitest's default reporter shows ❯ FAIL/PASS markers,
// stack traces with code frames, and summary lines like "Test Files 2 failed".
// Strategy: keep all FAIL blocks + summary; drop PASS spam.
#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>

namespace icmg::tkil {

class VitestFilter : public BaseFilter {
public:
    std::string name() const override { return "vitest"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        std::vector<std::string> kept;
        bool in_fail_block = false;
        std::regex re_fail(R"(❯|FAIL|×|AssertionError|Error:|at\s+.*\.(ts|tsx|js|jsx|mjs):\d+)",
                           std::regex::ECMAScript);
        std::regex re_summary(R"(Test Files|Tests|Duration|Snapshots|Start at|Fail|Pass)",
                              std::regex::ECMAScript);

        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];
            // Always keep summary footer
            if (std::regex_search(l, re_summary) && (l.find("failed") != std::string::npos ||
                                                       l.find("passed") != std::string::npos ||
                                                       l.find("Duration") != std::string::npos)) {
                kept.push_back(l);
                continue;
            }
            // FAIL marker → enter block
            if (l.find("FAIL ") != std::string::npos || l.find("❯ ") != std::string::npos) {
                in_fail_block = true;
                kept.push_back(l);
                continue;
            }
            // Empty line ends fail block
            if (in_fail_block && l.empty()) {
                in_fail_block = false;
                kept.push_back(l);
                continue;
            }
            if (in_fail_block) { kept.push_back(l); continue; }
            // Stack trace line outside fail block
            if (std::regex_search(l, re_fail)) kept.push_back(l);
        }
        if (kept.empty() && !lines.empty()) kept.push_back(lines.back());
        for (auto& l : kept) res.output += l + "\n";
        res.filtered_lines = (int)kept.size();
        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("vitest", VitestFilter);

} // namespace icmg::tkil

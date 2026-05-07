#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <algorithm>

namespace icmg::tkil {

class BuildFilter : public BaseFilter {
public:
    std::string name() const override { return "build"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        static const std::vector<std::string> keep_kw = {
            "error", "warning", "error[", "warning[",
            "failed", "failure", "link", "undefined reference",
            "cannot", "undefined", "undeclared", "expected",
            "fatal", "aborting", "panicked", "note:", "help:",
            "build successful", "build failed", "0 error", "0 warning",
            "errors generated", "warnings generated",
        };
        static const std::vector<std::string> skip_kw = {
            "compiling ", "downloaded ", "downloading ",
            "blocking waiting", "fresh ", "resolving ",
            "   Locking ", "   Updating ", "   Fetching ",
            "progress:", "[", "Installing"
        };

        std::vector<std::string> kept;
        for (size_t i = 0; i < lines.size(); ++i) {
            auto& l = lines[i];
            // Always keep last line (summary)
            if (i == lines.size() - 1 && !l.empty()) { kept.push_back(l); continue; }
            // Skip noise
            if (containsAny(l, skip_kw)) continue;
            // Keep errors/warnings
            if (containsAny(l, keep_kw)) { kept.push_back(l); continue; }
        }

        if (kept.empty() && !lines.empty()) {
            // All good — no errors. Show last 3 lines as summary
            size_t start = lines.size() > 3 ? lines.size() - 3 : 0;
            for (size_t i = start; i < lines.size(); ++i) kept.push_back(lines[i]);
        }

        for (auto& l : kept) res.output += l + "\n";
        res.filtered_lines = (int)kept.size();
        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("build", BuildFilter);

} // namespace icmg::tkil

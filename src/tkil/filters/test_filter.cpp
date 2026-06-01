#include "filter_utils.hpp"
#include "../../core/registry.hpp"

namespace icmg::tkil {

class TestFilter : public BaseFilter {
public:
    std::string name() const override { return "test"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        static const std::vector<std::string> keep_kw = {
            "failed", "failure", "error", "panic", "assert",
            "FAIL", "FAILED", "ERROR", "PANIC",
            "thread '", "assertion", "expected", "got",
            "test result:", "failures:", "---- ", "STDOUT----",
            "not ok", "# FAILED", "AssertionError",
            "tests passed", "tests failed", "test failed",
        };
        static const std::vector<std::string> skip_kw = {
            "running ", "... ok", "ok ", "test ", "test::",
            "Ran ", " ok (", "PASSED",
        };

        std::vector<std::string> kept;
        bool in_failure_block = false;

        for (size_t i = 0; i < lines.size(); ++i) {
            auto& l = lines[i];
            // Last line: always keep (summary)
            if (i == lines.size() - 1 && !l.empty()) { kept.push_back(l); continue; }

            // Start of failure block (Rust/Go style)
            if (l.find("---- ") == 0 || l.find("--- FAIL") == 0) {
                in_failure_block = true;
                kept.push_back(l);
                continue;
            }
            // End of failure block
            if (in_failure_block && (l.find("----") == 0 || l.empty())) {
                in_failure_block = false;
                kept.push_back(l);
                continue;
            }
            if (in_failure_block) { kept.push_back(l); continue; }

            if (containsAny(l, skip_kw)) continue;
            if (containsAny(l, keep_kw)) { kept.push_back(l); continue; }
        }

        if (kept.empty() && !lines.empty()) {
            // All passed — show last 2 lines
            size_t start = lines.size() > 2 ? lines.size() - 2 : 0;
            for (size_t i = start; i < lines.size(); ++i) kept.push_back(lines[i]);
        }

        for (auto& l : kept) res.output += l + "\n";
        res.filtered_lines = (int)kept.size();
        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("test", TestFilter);

} // namespace icmg::tkil

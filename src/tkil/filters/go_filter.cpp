// v1.21.3 (F3): Go (go build / test / vet / golangci-lint) filter.
//
// Strips: "go: downloading X v1.0.0", "go: finding", "ok  pkg  0.123s" (passing
// test pkg summary noise unless --verbose). Keeps: "--- FAIL: TestX", FAIL/PASS
// summary, panic + goroutine traces, vet warnings, file:line:col error markers.

#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>

namespace icmg::tkil {

class GoFilter : public BaseFilter {
public:
    std::string name() const override { return "go"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        static const std::regex re_keep(
            R"(^--- (FAIL|PASS|SKIP):|^FAIL\b|^PASS\b|^ok\s+\S+\s+\S+\s+\[.*FAIL|panic:|goroutine \d+ |\bvet:|^\S+\.go:\d+:\d+:|^# |^\s*Error Trace:|^\s*Error:|^\s*Test:|^\s*Messages:|expected:|actual:)",
            std::regex::ECMAScript);
        static const std::regex re_skip(
            R"(^go: downloading |^go: finding |^go: extracting |^go: writing |^go: added |^go: upgraded |^go: removed )",
            std::regex::ECMAScript);

        std::vector<std::string> kept;
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];
            if (i == lines.size() - 1 && !l.empty()) { kept.push_back(l); continue; }
            if (std::regex_search(l, re_skip)) continue;
            if (std::regex_search(l, re_keep)) { kept.push_back(l); continue; }
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

ICMG_REGISTER_FILTER("go", GoFilter);

} // namespace icmg::tkil

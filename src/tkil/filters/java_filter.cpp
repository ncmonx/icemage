// v1.21.3 (F3): Java (Maven + Gradle) filter.
//
// Maven floods with "[INFO]" + dependency download lines. Gradle floods with
// task progress and incremental cache hits. Keeps: [ERROR], [WARNING], BUILD
// SUCCESS/FAILURE, stack traces, "Tests run:" summary, compile errors with
// file:line:col, "FAILED" task lines.

#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>

namespace icmg::tkil {

class JavaFilter : public BaseFilter {
public:
    std::string name() const override { return "java"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        static const std::regex re_keep(
            R"(\[ERROR\]|\[WARNING\]|BUILD SUCCESS|BUILD FAILURE|BUILD FAILED|^Tests run:|\bFAILED\b|\bSKIPPED\b|^\s*at \S+\(\S+\.java:\d+\)|^\S+\.(java|kt|kts):\d+:\s*error:|^Caused by:|^Exception in thread|\bAssertionError\b|\bAssertionFailedError\b|\.java:\d+: (error|warning):|^FAILURE: Build failed|^>\s*Task :.*FAILED)",
            std::regex::ECMAScript);
        static const std::regex re_skip(
            R"(^\[INFO\] |^Downloading from |^Downloaded from |^Progress \(\d+\):|^> Task :\S+ (UP-TO-DATE|NO-SOURCE|FROM-CACHE|SKIPPED)$|^\s*<==========|^\s*=+>)",
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

ICMG_REGISTER_FILTER("java", JavaFilter);

} // namespace icmg::tkil

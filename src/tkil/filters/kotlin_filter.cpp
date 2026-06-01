// v1.21.3 (F3): Kotlin (kotlinc / gradle :compileKotlin) filter.
//
// Shares much with Java (gradle backbone) but adds Kotlin-specific markers:
// "e:", "w:", "i:" line prefixes from kotlinc, "BUILD FAILED in Xs", "Type
// mismatch", compile errors with `.kt:` path.

#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>

namespace icmg::tkil {

class KotlinFilter : public BaseFilter {
public:
    std::string name() const override { return "kotlin"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        static const std::regex re_keep(
            R"(^e: |^w: |^i: |\.kts?:\d+:\d+:\s*(error|warning):|^BUILD SUCCESSFUL|^BUILD FAILED|^FAILURE: Build failed|^>\s*Task :.*FAILED|^Tests? (Passed|Failed):|^\s*at \S+\(\S+\.kt:\d+\)|^Caused by:|Exception in thread|^\s*\* What went wrong:)",
            std::regex::ECMAScript);
        static const std::regex re_skip(
            R"(^> Task :\S+ (UP-TO-DATE|NO-SOURCE|FROM-CACHE|SKIPPED)$|^\s*<==========|^\s*=+>|^Downloading https|^Picked up _JAVA_OPTIONS|^Welcome to Gradle)",
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

ICMG_REGISTER_FILTER("kotlin", KotlinFilter);

} // namespace icmg::tkil

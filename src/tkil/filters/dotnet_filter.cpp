// v1.21.3 (F3): .NET (dotnet / msbuild) filter.
//
// MSBuild emits target-progress + restore noise. Keeps: error/warning lines
// with code "CS0123" / "MSB1234", "Build succeeded/FAILED", test summary
// "Passed:/Failed:/Skipped:", stack traces, file:line markers.

#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>

namespace icmg::tkil {

class DotnetFilter : public BaseFilter {
public:
    std::string name() const override { return "dotnet"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        static const std::regex re_keep(
            R"(\b(error|warning) [A-Z]{2,4}\d+\b|^Build succeeded|^Build FAILED|^\s*Passed!|^\s*Failed!|Passed:\s*\d+|Failed:\s*\d+|Skipped:\s*\d+|^Test Run|^Total tests:|FAILED\s+\S+\.|^Unhandled exception:|^Exception:|\s+at \S+\.(cs|fs|vb)\(\d+\)|^\s*Stack Trace:)",
            std::regex::ECMAScript);
        static const std::regex re_skip(
            R"(^\s*Restored |^\s*Determining projects to restore|^\s*Restore complete|^MSBuild version |^\s*Generating |^Copyright \(C\) Microsoft Corporation|^\s+\d+\.\d+\.\d+\.\d+ ->)",
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

ICMG_REGISTER_FILTER("dotnet", DotnetFilter);

} // namespace icmg::tkil

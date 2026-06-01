// v1.21.3 (F3): Swift (swift / xcodebuild) filter.
//
// xcodebuild emits massive CompileC noise + linker invocations. Keeps: errors
// with "error:" + "warning:" + Swift-specific "/path/Foo.swift:12:34: error",
// "** BUILD FAILED **" / "** BUILD SUCCEEDED **", "** TEST FAILED **",
// XCTest assertion failures, "Test Suite ... failed".

#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>

namespace icmg::tkil {

class SwiftFilter : public BaseFilter {
public:
    std::string name() const override { return "swift"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        static const std::regex re_keep(
            R"(\.swift:\d+:\d+:\s*(error|warning|note):|^\*\* BUILD (FAILED|SUCCEEDED) \*\*|^\*\* TEST (FAILED|SUCCEEDED) \*\*|^Test Suite .* (started|passed|failed)|XCTAssert\w+ failed|^Compiling failed:|^error:|fatal error:|Build complete!|Compiling for |^\s+/\S+\.swift:\d+:|^Executed \d+ tests?,)",
            std::regex::ECMAScript);
        static const std::regex re_skip(
            R"(^CompileC |^Ld |^CompileSwift |^MergeSwiftModule |^GenerateDSYMFile |^CodeSign |^Touch |^Validate |^CompileSwiftSources |^CreateBuildDirectory |^ProcessInfoPlistFile |^Copy |^\s*cd \S+|^\s*export |^\s+/usr/bin/clang)",
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

ICMG_REGISTER_FILTER("swift", SwiftFilter);

} // namespace icmg::tkil

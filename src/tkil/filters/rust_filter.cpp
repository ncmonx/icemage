// v1.21.3 (F3): Rust / cargo filter.
//
// Strips: "   Compiling X v1.2.3 (...)", "    Blocking waiting", "    Finished",
// "Downloaded N crates", progress bars. Keeps: error[E0XXX], warning, ^^^ markers,
// "test result: ok|FAILED" lines, panic backtraces (note/help), final summary.

#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>

namespace icmg::tkil {

class RustFilter : public BaseFilter {
public:
    std::string name() const override { return "rust"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        static const std::regex re_keep(
            R"(\berror(\[E\d+\])?\b|\bwarning(\[\w+\])?\b|\bpanicked\b|\bnote:|\bhelp:|\bcaused by:|\^\^\^|\| \^|^test result:|^running \d+ tests|---- .* stdout ----|^test \w+ \.\.\. (ok|FAILED|ignored)|errors? emitted|^Finished )",
            std::regex::ECMAScript | std::regex::icase);
        static const std::regex re_skip(
            R"(^\s*Compiling |^\s*Checking |^\s*Downloaded |^\s*Downloading |^\s*Blocking |^\s*Fresh |^\s*Updating |^\s*Locking |^\s*Compiling$|^\s*Running )",
            std::regex::ECMAScript);

        std::vector<std::string> kept;
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];
            if (i == lines.size() - 1 && !l.empty()) { kept.push_back(l); continue; }
            if (std::regex_search(l, re_skip)) continue;
            if (std::regex_search(l, re_keep)) { kept.push_back(l); continue; }
            // Indented context lines under errors (column markers, --> file:line).
            if (l.find(" --> ") != std::string::npos
                || l.find(" |") == 0
                || l.find("  |") == 0) {
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

ICMG_REGISTER_FILTER("rust", RustFilter);

} // namespace icmg::tkil

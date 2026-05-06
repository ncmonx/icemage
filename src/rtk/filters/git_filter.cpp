#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>
#include <algorithm>

namespace icmg::rtk {

class GitFilter : public BaseFilter {
public:
    std::string name() const override { return "git"; }

    FilterResult filter(const std::string& raw, const std::string& command) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        // Determine git subcommand
        bool is_diff   = command.find("git diff")   != std::string::npos ||
                         command.find("git show")   != std::string::npos;
        bool is_log    = command.find("git log")    != std::string::npos;
        bool is_status = command.find("git status") != std::string::npos;

        std::vector<std::string> kept;

        if (is_status) {
            // git status: keep as-is (usually short)
            kept = lines;
        } else if (is_log) {
            // git log: max 30 entries, shorten hash to 8 chars
            static const std::regex re_hash(R"(^([0-9a-f]{8,40})\b)");
            int entry_count = 0;
            for (auto& l : lines) {
                if (entry_count >= 30) {
                    kept.push_back("... (truncated at 30 entries)");
                    break;
                }
                std::smatch m;
                std::string out_line = l;
                if (std::regex_search(l, m, re_hash)) {
                    // Shorten hash to 8 chars
                    out_line = l.substr(0, 8) + l.substr(m[0].length());
                    ++entry_count;
                }
                kept.push_back(out_line);
            }
        } else if (is_diff) {
            // git diff/show: changed lines (+/-) + 3 context, strip binary
            static const std::regex re_binary(R"(Binary files .* differ)");
            bool in_hunk = false;
            int  ctx_remaining = 0;
            std::string prev_context;

            for (auto& l : lines) {
                // Skip binary diff markers
                if (std::regex_search(l, re_binary)) {
                    kept.push_back(l); continue;
                }
                // Header lines (---, +++, @@, diff --git, index, etc.)
                if (l.empty() || l[0] == 'd' || l[0] == 'i' ||
                    (l.size() >= 3 && l.substr(0,3) == "---") ||
                    (l.size() >= 3 && l.substr(0,3) == "+++") ||
                    (l.size() >= 2 && l.substr(0,2) == "@@")) {
                    kept.push_back(l);
                    if (l.size() >= 2 && l[0] == '@') in_hunk = true;
                    continue;
                }
                if (!in_hunk) continue;

                if (l[0] == '+' || l[0] == '-') {
                    kept.push_back(l);
                    ctx_remaining = 3;
                } else if (ctx_remaining > 0) {
                    kept.push_back(l);
                    --ctx_remaining;
                }
            }
        } else {
            kept = lines;
        }

        for (auto& l : kept) res.output += l + "\n";
        res.filtered_lines = (int)kept.size();
        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("git", GitFilter);

} // namespace icmg::rtk

#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <unordered_map>
#include <vector>
#include <regex>

namespace icmg::tkil {

class SearchFilter : public BaseFilter {
public:
    std::string name() const override { return "search"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        // Group by file (file:line:content format)
        // rg/grep output: "path/file.cpp:42:content"
        static const std::regex re_match(R"(^([^:]+):(\d+):(.*)$)");

        std::unordered_map<std::string, std::vector<std::string>> by_file;
        std::vector<std::string> file_order;
        int total = 0;

        for (auto& l : lines) {
            std::smatch m;
            if (std::regex_match(l, m, re_match)) {
                std::string file = m[1].str();
                std::string entry = file + ":" + m[2].str() + ": " + m[3].str();
                if (by_file.find(file) == by_file.end()) file_order.push_back(file);
                by_file[file].push_back(entry);
                ++total;
            }
        }

        constexpr int MAX_SEARCH_LINES = 200;
        int shown = 0;
        std::string out;

        // v1.28.0 #G: per-file count summary when truncating. Previously
        // emitted "N more matches in remaining files" — opaque blob. Now
        // lists the suppressed files with hit counts so caller can pick.
        int suppress_start_idx = -1;
        for (size_t fi = 0; fi < file_order.size(); ++fi) {
            if (shown >= MAX_SEARCH_LINES) { suppress_start_idx = (int)fi; break; }
            auto& matches = by_file[file_order[fi]];
            for (size_t ei = 0; ei < matches.size(); ++ei) {
                if (shown >= MAX_SEARCH_LINES) {
                    out += "... +" + std::to_string(matches.size() - ei)
                         + " more in " + file_order[fi] + "\n";
                    suppress_start_idx = (int)fi + 1;
                    goto done;
                }
                out += matches[ei] + "\n";
                ++shown;
            }
        }
        done:;
        if (suppress_start_idx >= 0
            && (size_t)suppress_start_idx < file_order.size()) {
            out += "Suppressed (per-file):\n";
            int suppressed_total = 0;
            for (size_t fi = (size_t)suppress_start_idx; fi < file_order.size(); ++fi) {
                int cnt = (int)by_file[file_order[fi]].size();
                suppressed_total += cnt;
                out += "  " + std::to_string(cnt) + "  " + file_order[fi] + "\n";
            }
            out += "Total suppressed: " + std::to_string(suppressed_total)
                 + " match(es) across "
                 + std::to_string(file_order.size() - (size_t)suppress_start_idx)
                 + " file(s).\n";
        }

        if (out.empty() && !lines.empty()) {
            // Unrecognized format — show raw up to MAX_SEARCH_LINES
            for (int i = 0; i < std::min((int)lines.size(), MAX_SEARCH_LINES); ++i)
                out += lines[i] + "\n";
            shown = (int)std::min((int)lines.size(), MAX_SEARCH_LINES);
        }

        res.output = out;
        res.filtered_lines = shown;
        res.was_truncated = (shown < total);
        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("search", SearchFilter);

} // namespace icmg::tkil

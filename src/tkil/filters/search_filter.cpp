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

        for (auto& f : file_order) {
            if (shown >= MAX_SEARCH_LINES) break;
            auto& matches = by_file[f];
            for (auto& entry : matches) {
                if (shown >= MAX_SEARCH_LINES) {
                    out += "... " + std::to_string(total - shown)
                         + " more matches in remaining files ...\n";
                    goto done;
                }
                out += entry + "\n";
                ++shown;
            }
        }
        done:;

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

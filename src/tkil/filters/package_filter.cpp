#include "filter_utils.hpp"
#include "../../core/registry.hpp"

namespace icmg::tkil {

// A5: npm/yarn/pip/gem filter
class PackageManagerFilter : public BaseFilter {
public:
    std::string name() const override { return "npm"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        static const std::vector<std::string> keep_kw = {
            "warn", "error", "err!", "npm warn", "npm error",
            "added ", "removed ", "updated ", "audited ",
            "successfully installed", "successfully built",
            "packages installed", "packages updated",
            "vulnerabilit", "found ", "requires",
            "WARN", "ERROR", "WARNING",
        };
        static const std::vector<std::string> skip_kw = {
            "progress ", "extract:", "http fetch",
            "idealTree", "reify:", "timing ",
            "Downloading", "Unpacking", "Collecting",
            "Already up-to-date", "Building wheel",
        };

        std::vector<std::string> kept;
        for (size_t i = 0; i < lines.size(); ++i) {
            auto& l = lines[i];
            if (i == lines.size() - 1 && !l.empty()) { kept.push_back(l); continue; }
            if (containsAny(l, skip_kw)) continue;
            if (containsAny(l, keep_kw)) { kept.push_back(l); continue; }
        }
        if (kept.empty() && !lines.empty()) {
            size_t start = lines.size() > 3 ? lines.size() - 3 : 0;
            for (size_t i = start; i < lines.size(); ++i) kept.push_back(lines[i]);
        }

        for (auto& l : kept) res.output += l + "\n";
        res.filtered_lines = (int)kept.size();
        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("npm", PackageManagerFilter);

} // namespace icmg::tkil

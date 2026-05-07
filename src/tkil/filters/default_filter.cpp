#include "filter_utils.hpp"
#include "../../core/registry.hpp"

namespace icmg::tkil {

class DefaultFilter : public BaseFilter {
public:
    std::string name() const override { return "default"; }

    FilterResult filter(const std::string& raw, const std::string& /*cmd*/) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        constexpr int HEAD = 50;
        constexpr int TAIL = 20;

        if ((int)lines.size() <= HEAD + TAIL) {
            for (auto& l : lines) res.output += l + "\n";
            res.filtered_lines = (int)lines.size();
            return res;
        }

        // Head
        for (int i = 0; i < HEAD; ++i) res.output += lines[i] + "\n";
        int omitted = (int)lines.size() - HEAD - TAIL;
        res.output += "\n... (" + std::to_string(omitted) + " lines omitted) ...\n\n";
        // Tail
        for (int i = (int)lines.size() - TAIL; i < (int)lines.size(); ++i)
            res.output += lines[i] + "\n";

        res.filtered_lines = HEAD + TAIL;
        res.was_truncated  = true;
        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("default", DefaultFilter);

} // namespace icmg::tkil

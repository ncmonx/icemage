#include "base_extractor.hpp"
#include "../../core/registry.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

class RustExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override { return {".rs"}; }

    ExtractResult extract(const std::string& /*path*/,
                          const std::string& content) override {
        ExtractResult res;
        std::istringstream ss(content);
        std::string line;

        static const std::regex re_use    (R"(^use\s+([\w:]+))");
        static const std::regex re_struct (R"(^(?:pub\s+)?struct\s+(\w+))");
        static const std::regex re_enum   (R"(^(?:pub\s+)?enum\s+(\w+))");
        static const std::regex re_impl   (R"(^impl(?:<[^>]*>)?\s+(\w+))");
        static const std::regex re_fn     (R"(^(?:pub\s+)?(?:async\s+)?fn\s+(\w+)\s*[<(])");

        // First doc comment block (///)
        bool context_done = false;
        std::string doc_acc;
        std::smatch m;

        while (std::getline(ss, line)) {
            if (!context_done) {
                if (line.rfind("///", 0) == 0) {
                    doc_acc += line.substr(3) + " ";
                } else if (!doc_acc.empty()) {
                    res.context = doc_acc;
                    context_done = true;
                }
            }

            if (std::regex_search(line, m, re_use))    res.imports.push_back(m[1].str());
            if (std::regex_search(line, m, re_struct))  res.classes.push_back(m[1].str());
            else if (std::regex_search(line, m, re_enum)) res.classes.push_back(m[1].str());
            else if (std::regex_search(line, m, re_impl)) res.classes.push_back(m[1].str() + " (impl)");
            if (std::regex_search(line, m, re_fn))     res.functions.push_back(m[1].str());
        }

        auto dedup = [](std::vector<std::string>& v) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        dedup(res.imports); dedup(res.classes); dedup(res.functions);
        return res;
    }
};

ICMG_REGISTER_EXTRACTOR("rust", RustExtractor);

} // namespace icmg::graph

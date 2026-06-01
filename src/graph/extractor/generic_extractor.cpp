#include "base_extractor.hpp"
#include <sstream>
#include <algorithm>

namespace icmg::graph {

// Generic fallback: no ICMG_REGISTER_EXTRACTOR (used explicitly by Scanner)
class GenericExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override { return {}; }

    ExtractResult extract(const std::string& /*path*/,
                          const std::string& content) override {
        ExtractResult res;
        std::istringstream ss(content);
        std::string line;
        bool first = true;

        while (std::getline(ss, line)) {
            // Context = first non-empty, non-comment line
            if (first && !line.empty() && line[0] != '#' && line[0] != '/') {
                res.context = line.substr(0, 120);
                first = false;
            }
            // Anything with '(' in the middle looks like a declaration
            auto pos = line.find('(');
            if (pos != std::string::npos && pos > 0 && pos < line.size() - 1) {
                // Extract word before '('
                std::string candidate;
                size_t i = pos;
                while (i > 0 && (std::isalnum((unsigned char)line[i-1]) || line[i-1] == '_')) {
                    candidate = line[--i];
                    // build it properly
                }
                // Simpler approach: split by space, grab token containing '('
                std::istringstream ls(line);
                std::string tok;
                while (ls >> tok) {
                    auto p = tok.find('(');
                    if (p != std::string::npos && p > 0) {
                        std::string fn = tok.substr(0, p);
                        if (fn.size() >= 2 &&
                            std::all_of(fn.begin(), fn.end(),
                                [](char c){ return std::isalnum((unsigned char)c) || c=='_'; }))
                            res.functions.push_back(fn);
                        break;
                    }
                }
            }
        }

        std::sort(res.functions.begin(), res.functions.end());
        res.functions.erase(std::unique(res.functions.begin(), res.functions.end()),
                            res.functions.end());
        return res;
    }
};

// Exposed for Scanner to instantiate directly
BaseExtractor* makeGenericExtractor() {
    static GenericExtractor inst;
    return &inst;
}

} // namespace icmg::graph

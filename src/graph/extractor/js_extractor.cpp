#include "base_extractor.hpp"
#include "../../core/registry.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

class JsExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override {
        return {".js", ".jsx", ".ts", ".tsx", ".mjs"};
    }

    ExtractResult extract(const std::string& /*path*/,
                          const std::string& content) override {
        ExtractResult res;
        std::istringstream ss(content);
        std::string line;

        static const std::regex re_import  (R"((?:import|require)\s*(?:\{[^}]*\}\s*from\s*)?['"]([^'"]+)['"])");
        static const std::regex re_class   (R"((?:export\s+)?class\s+(\w+))");
        static const std::regex re_func1   (R"((?:export\s+)?(?:async\s+)?function\s+(\w+)\s*\()");
        static const std::regex re_func2   (R"((?:export\s+)?const\s+(\w+)\s*=\s*(?:async\s*)?\()");
        static const std::regex re_func3   (R"((?:export\s+)?const\s+(\w+)\s*=\s*(?:async\s+)?\([^)]*\)\s*=>)");

        bool in_jsdoc    = false;
        bool context_done = false;
        std::string doc_acc;
        std::smatch m;

        while (std::getline(ss, line)) {
            // JSDoc context
            if (!context_done) {
                if (!in_jsdoc && line.find("/**") != std::string::npos) {
                    in_jsdoc = true;
                    doc_acc += line.substr(line.find("/**") + 3) + " ";
                } else if (in_jsdoc) {
                    if (line.find("*/") != std::string::npos) {
                        doc_acc += line.substr(0, line.find("*/"));
                        res.context = doc_acc;
                        context_done = true; in_jsdoc = false;
                    } else {
                        // Strip leading * from JSDoc lines
                        std::string stripped = line;
                        auto star = stripped.find('*');
                        if (star != std::string::npos && star < 4) stripped = stripped.substr(star + 1);
                        doc_acc += stripped + " ";
                    }
                }
            }

            if (std::regex_search(line, m, re_import))  res.imports.push_back(m[1].str());
            if (std::regex_search(line, m, re_class))   res.classes.push_back(m[1].str());
            if (std::regex_search(line, m, re_func1))   res.functions.push_back(m[1].str());
            else if (std::regex_search(line, m, re_func2)) res.functions.push_back(m[1].str());
            else if (std::regex_search(line, m, re_func3)) res.functions.push_back(m[1].str());
        }

        auto dedup = [](std::vector<std::string>& v) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        dedup(res.imports); dedup(res.classes); dedup(res.functions);
        return res;
    }
};

ICMG_REGISTER_EXTRACTOR("js", JsExtractor);

} // namespace icmg::graph

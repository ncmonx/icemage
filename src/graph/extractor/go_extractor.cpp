#include "base_extractor.hpp"
#include "../../core/registry.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

class GoExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override { return {".go"}; }

    ExtractResult extract(const std::string& /*path*/,
                          const std::string& content) override {
        ExtractResult res;
        std::istringstream ss(content);
        std::string line;

        static const std::regex re_import(R"(^\s*"([\w./]+)")");
        static const std::regex re_struct(R"(^type\s+(\w+)\s+struct)");
        static const std::regex re_iface (R"(^type\s+(\w+)\s+interface)");
        static const std::regex re_func1 (R"(^func\s+(\w+)\s*\()");
        static const std::regex re_func2 (R"(^func\s+\(\w+\s+\*?\w+\)\s+(\w+)\s*\()");

        bool in_import = false;
        std::smatch m;

        while (std::getline(ss, line)) {
            if (line.find("import (") != std::string::npos) { in_import = true; continue; }
            if (in_import) {
                if (line.find(')') != std::string::npos) { in_import = false; continue; }
                if (std::regex_search(line, m, re_import)) res.imports.push_back(m[1].str());
                continue;
            }
            if (line.rfind("import ", 0) == 0) {
                // single import
                if (std::regex_search(line, m, re_import)) res.imports.push_back(m[1].str());
            }
            if (std::regex_search(line, m, re_struct)) res.classes.push_back(m[1].str());
            else if (std::regex_search(line, m, re_iface)) res.classes.push_back(m[1].str());
            if (std::regex_search(line, m, re_func2))  res.functions.push_back(m[1].str());
            else if (std::regex_search(line, m, re_func1)) res.functions.push_back(m[1].str());
        }

        auto dedup = [](std::vector<std::string>& v) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        dedup(res.imports); dedup(res.classes); dedup(res.functions);
        return res;
    }
};

ICMG_REGISTER_EXTRACTOR("go", GoExtractor);

} // namespace icmg::graph

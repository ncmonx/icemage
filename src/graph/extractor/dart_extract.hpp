#pragma once
// Dart import/symbol extraction (regex, js/php/ruby family). Pure + testable.
//   import 'x'                    -> imports
//   class/mixin/enum/extension X  -> classes
//   ReturnType name(...) { / =>   -> functions (top-level/method, best-effort)
#include "base_extractor.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

inline ExtractResult extractDart(const std::string& content) {
    ExtractResult res;
    static const std::regex re_import(R"(^\s*(?:export|import)\s+['"]([^'"]+)['"])");
    static const std::regex re_type(
        R"(^\s*(?:abstract\s+|sealed\s+|final\s+|base\s+|interface\s+)*(?:class|mixin|enum|extension)\s+(\w+))");
    static const std::regex re_func(
        R"(^\s*(?:static\s+|final\s+|const\s+)*[\w<>,?\[\] ]+\s+(\w+)\s*\([^;{]*\)\s*(?:async\*?\s*)?[\{=])");
    std::istringstream ss(content);
    std::string line; std::smatch m;
    while (std::getline(ss, line)) {
        if (std::regex_search(line, m, re_import)) res.imports.push_back(m[1].str());
        if (std::regex_search(line, m, re_type))   res.classes.push_back(m[1].str());
        else if (std::regex_search(line, m, re_func)) {
            std::string fn = m[1].str();
            if (fn != "if" && fn != "for" && fn != "while" && fn != "switch" && fn != "catch")
                res.functions.push_back(fn);
        }
    }
    auto dedup = [](std::vector<std::string>& v) {
        std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); };
    dedup(res.imports); dedup(res.classes); dedup(res.functions);
    return res;
}

}  // namespace icmg::graph

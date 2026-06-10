#pragma once
// Swift import/symbol extraction (regex, js/php/ruby family). Pure + testable.
//   import X                              -> imports
//   class/struct/enum/protocol/extension/actor X -> classes
//   func name                            -> functions
#include "base_extractor.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

inline ExtractResult extractSwift(const std::string& content) {
    ExtractResult res;
    static const std::regex re_import(R"(^\s*import\s+(\w+))");
    static const std::regex re_type(
        R"(^\s*(?:public\s+|private\s+|internal\s+|fileprivate\s+|open\s+|final\s+)*(?:class|struct|enum|protocol|extension|actor)\s+(\w+))");
    static const std::regex re_func(
        R"(^\s*(?:public\s+|private\s+|internal\s+|fileprivate\s+|open\s+|final\s+|static\s+|class\s+|override\s+|mutating\s+)*func\s+(\w+))");
    std::istringstream ss(content);
    std::string line; std::smatch m;
    while (std::getline(ss, line)) {
        if (std::regex_search(line, m, re_import)) res.imports.push_back(m[1].str());
        if (std::regex_search(line, m, re_type))   res.classes.push_back(m[1].str());
        if (std::regex_search(line, m, re_func))   res.functions.push_back(m[1].str());
    }
    auto dedup = [](std::vector<std::string>& v) {
        std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); };
    dedup(res.imports); dedup(res.classes); dedup(res.functions);
    return res;
}

}  // namespace icmg::graph

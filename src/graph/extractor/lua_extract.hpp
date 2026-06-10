#pragma once
// Lua import/symbol extraction (regex, js/php/ruby family). Pure + testable.
//   require "x"                          -> imports
//   function name / function T.m / local function n / n = function -> functions
// (Lua has no real classes; tables are conventional, so only functions+imports.)
#include "base_extractor.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

inline ExtractResult extractLua(const std::string& content) {
    ExtractResult res;
    static const std::regex re_require(R"(require\s*\(?\s*['"]([^'"]+)['"])");
    static const std::regex re_func   (R"(^\s*(?:local\s+)?function\s+([\w.:]+))");
    static const std::regex re_assign (R"(^\s*(?:local\s+)?([\w.]+)\s*=\s*function\b)");
    std::istringstream ss(content);
    std::string line; std::smatch m;
    while (std::getline(ss, line)) {
        if (std::regex_search(line, m, re_require)) res.imports.push_back(m[1].str());
        if (std::regex_search(line, m, re_func))    res.functions.push_back(m[1].str());
        else if (std::regex_search(line, m, re_assign)) res.functions.push_back(m[1].str());
    }
    auto dedup = [](std::vector<std::string>& v) {
        std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); };
    dedup(res.imports); dedup(res.functions);
    return res;
}

}  // namespace icmg::graph

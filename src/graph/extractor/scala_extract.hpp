#pragma once
// Scala import/symbol extraction (regex, js/php/ruby family). Pure + testable.
//   package X                  -> namespaces
//   import X                   -> imports
//   class/object/trait X        -> classes
//   def name                   -> functions
#include "base_extractor.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

inline ExtractResult extractScala(const std::string& content) {
    ExtractResult res;
    static const std::regex re_pkg   (R"(^\s*package\s+([\w.]+))");
    static const std::regex re_import(R"(^\s*import\s+([\w.]+))");
    static const std::regex re_type(
        R"(^\s*(?:abstract\s+|final\s+|sealed\s+|case\s+|implicit\s+|private\s+|protected\s+)*(?:class|object|trait)\s+(\w+))");
    static const std::regex re_def(
        R"(^\s*(?:override\s+|private\s+|protected\s+|final\s+|implicit\s+|lazy\s+)*def\s+(\w+))");
    std::istringstream ss(content);
    std::string line; std::smatch m;
    while (std::getline(ss, line)) {
        if (std::regex_search(line, m, re_pkg))    res.namespaces.push_back(m[1].str());
        if (std::regex_search(line, m, re_import)) res.imports.push_back(m[1].str());
        if (std::regex_search(line, m, re_type))   res.classes.push_back(m[1].str());
        if (std::regex_search(line, m, re_def))    res.functions.push_back(m[1].str());
    }
    auto dedup = [](std::vector<std::string>& v) {
        std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); };
    dedup(res.imports); dedup(res.namespaces); dedup(res.classes); dedup(res.functions);
    return res;
}

}  // namespace icmg::graph

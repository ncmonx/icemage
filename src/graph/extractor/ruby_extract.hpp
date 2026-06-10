#pragma once
// Ruby import/symbol extraction (regex, same family as js/php extractors).
// Pure free function so it is unit-testable without the registry; the
// registered RubyExtractor (ruby_extractor.cpp) is a thin wrapper.
//   require / require_relative / load -> imports
//   module X      -> namespaces
//   class X (< Y) -> classes
//   def x / def self.x -> functions
#include "base_extractor.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

inline ExtractResult extractRuby(const std::string& content) {
    ExtractResult res;
    static const std::regex re_require(
        R"((?:require_relative|require|load)\s*\(?\s*['"]([^'"]+)['"])");
    static const std::regex re_module (R"(^\s*module\s+([A-Z][\w:]*))");
    static const std::regex re_class  (R"(^\s*class\s+([A-Z][\w:]*))");
    static const std::regex re_def    (R"(^\s*def\s+(?:self\.)?([A-Za-z_]\w*[?!=]?))");

    std::istringstream ss(content);
    std::string line;
    std::smatch m;
    while (std::getline(ss, line)) {
        if (std::regex_search(line, m, re_require)) res.imports.push_back(m[1].str());
        if (std::regex_search(line, m, re_module))  res.namespaces.push_back(m[1].str());
        if (std::regex_search(line, m, re_class))   res.classes.push_back(m[1].str());
        if (std::regex_search(line, m, re_def))     res.functions.push_back(m[1].str());
    }

    auto dedup = [](std::vector<std::string>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    dedup(res.imports); dedup(res.namespaces);
    dedup(res.classes); dedup(res.functions);
    return res;
}

}  // namespace icmg::graph

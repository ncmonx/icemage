#include "shell_extractor.hpp"
#include "../../core/registry.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

ExtractResult ShellExtractor::extract(const std::string& /*path*/,
                                       const std::string& content) {
    ExtractResult res;
    if (content.empty()) return res;

    std::istringstream ss(content);
    std::string line;

    // bash:   function foo() { / function foo {
    // posix:  foo() {
    // ps1:    function Foo {  / function Foo() {
    static const std::regex re_func_kw  (R"re(^\s*function\s+(\w+)\s*[\({])re");
    static const std::regex re_func_pos (R"re(^\s*(\w+)\s*\(\s*\)\s*\{)re");

    // source ./file.sh  or  . ./file.sh
    static const std::regex re_source   (R"re(^\s*source\s+(\S+))re");
    static const std::regex re_dot      (R"re(^\s*\.\s+(\S+))re");

    // ps1 dot-source:  . .\file.ps1
    static const std::regex re_ps1_dot  (R"re(^\s*\.\s+(\.\\[^\s]+))re");

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // skip comments
        std::string trimmed = line;
        size_t first = trimmed.find_first_not_of(" \t");
        if (first != std::string::npos && (trimmed[first] == '#')) continue;

        std::smatch m;

        // function keyword form (bash + ps1)
        if (std::regex_search(line, m, re_func_kw)) {
            res.functions.push_back(m[1].str());
            continue;
        }
        // posix form: foo() {
        if (std::regex_search(line, m, re_func_pos)) {
            std::string name = m[1].str();
            // filter shell keywords
            static const std::vector<std::string> kw = {
                "if","else","elif","fi","for","while","do","done",
                "case","esac","then","in","return","exit"};
            if (std::find(kw.begin(), kw.end(), name) == kw.end())
                res.functions.push_back(name);
            continue;
        }

        // source / dot-source
        if (std::regex_search(line, m, re_source)) {
            res.imports.push_back("sources:" + m[1].str());
            continue;
        }
        if (std::regex_search(line, m, re_ps1_dot)) {
            res.imports.push_back("sources:" + m[1].str());
            continue;
        }
        if (std::regex_search(line, m, re_dot)) {
            std::string tgt = m[1].str();
            // avoid matching ". somecommand" that is not a path
            if (tgt.size() > 1 && (tgt[0] == '.' || tgt[0] == '/'))
                res.imports.push_back("sources:" + tgt);
        }
    }

    auto dedup = [](std::vector<std::string>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    dedup(res.imports);
    dedup(res.functions);

    return res;
}

ICMG_REGISTER_EXTRACTOR("shell", ShellExtractor);

} // namespace icmg::graph

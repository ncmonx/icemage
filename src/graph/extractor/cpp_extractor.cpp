#include "cpp_extractor.hpp"
#include "../../core/registry.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

ExtractResult CppExtractor::extract(const std::string& /*path*/,
                                     const std::string& content) {
    ExtractResult res;
    std::istringstream ss(content);
    std::string line;

    static const std::regex re_include_local(R"re(#include\s+"([^"]+)")re");
    static const std::regex re_include_sys  (R"re(#include\s+<([^>]+)>)re");
    static const std::regex re_class        (R"((?:template\s*<[^>]*>\s*)?(?:class|struct)\s+(\w+))");
    static const std::regex re_func         (R"((?:^|\s)(?:static\s+|virtual\s+|inline\s+|explicit\s+)*(?:\w[\w:<>*&\s]+\s+)?(\w+)\s*\([^;{]*\)\s*(?:const\s*)?(?:override\s*)?(?:noexcept\s*)?\{?)");
    static const std::regex re_func_simple  (R"((?:^|\s)(\w+)\s*\([^;]*\)\s*\{)");

    bool in_block_comment = false;
    bool context_done     = false;
    std::string context_acc;
    int line_num = 0;

    std::smatch m;

    while (std::getline(ss, line)) {
        ++line_num;

        // Block comment context extraction
        if (!context_done) {
            if (!in_block_comment) {
                if (line.find("/*") != std::string::npos) {
                    in_block_comment = true;
                    auto s = line.find("/*") + 2;
                    auto e = line.find("*/", s);
                    if (e != std::string::npos) {
                        context_acc += line.substr(s, e - s) + " ";
                        in_block_comment = false;
                    } else {
                        context_acc += line.substr(s) + " ";
                    }
                } else if (line.find("//") != std::string::npos && line_num <= 5) {
                    context_acc += line.substr(line.find("//") + 2) + " ";
                } else if (line_num > 10) {
                    if (!context_acc.empty()) res.context = context_acc;
                    context_done = true;
                }
            } else {
                auto e = line.find("*/");
                if (e != std::string::npos) {
                    context_acc += line.substr(0, e) + " ";
                    in_block_comment = false;
                    res.context = context_acc;
                    context_done = true;
                } else {
                    context_acc += line + " ";
                }
            }
        }

        // #include
        if (std::regex_search(line, m, re_include_local)) {
            res.imports.push_back(m[1].str());
        } else if (std::regex_search(line, m, re_include_sys)) {
            res.imports.push_back(m[1].str());
        }

        // class / struct
        if (std::regex_search(line, m, re_class)) {
            std::string cls = m[1].str();
            // Filter false positives
            static const std::vector<std::string> kw = {
                "if", "else", "for", "while", "return", "switch", "case"};
            if (std::find(kw.begin(), kw.end(), cls) == kw.end())
                res.classes.push_back(cls);
        }

        // Functions: lines ending with { (definition, not declaration ending with ;)
        if (line.find('(') != std::string::npos &&
            (line.back() == '{' || line.find("){") != std::string::npos ||
             line.find(") {") != std::string::npos)) {
            if (std::regex_search(line, m, re_func_simple)) {
                std::string fn = m[1].str();
                static const std::vector<std::string> kw2 = {
                    "if", "else", "for", "while", "switch", "catch"};
                if (std::find(kw2.begin(), kw2.end(), fn) == kw2.end())
                    res.functions.push_back(fn);
            }
        }
    }

    if (!context_done && !context_acc.empty()) res.context = context_acc;

    // Trim context
    while (!res.context.empty() && (res.context.back() == ' ' || res.context.back() == '\n'))
        res.context.pop_back();

    // Deduplicate
    auto dedup = [](std::vector<std::string>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    dedup(res.imports);
    dedup(res.classes);
    dedup(res.functions);

    return res;
}

ICMG_REGISTER_EXTRACTOR("cpp", CppExtractor);

} // namespace icmg::graph

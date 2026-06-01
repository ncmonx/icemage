#include "base_extractor.hpp"
#include "../../core/registry.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

class JavaExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override {
        return {".java", ".kt", ".kts"};
    }

    ExtractResult extract(const std::string& /*path*/,
                          const std::string& content) override {
        ExtractResult res;
        std::istringstream ss(content);
        std::string line;

        static const std::regex re_import(R"(^import\s+([\w.]+);?)");
        static const std::regex re_pkg   (R"(^package\s+([\w.]+);?)");
        static const std::regex re_class (R"((?:public\s+|private\s+|protected\s+|abstract\s+|final\s+)*(?:class|interface|enum)\s+(\w+))");
        static const std::regex re_func  (R"((?:public|private|protected|static|final|abstract|synchronized|\s)+(?:[\w<>\[\]]+\s+)?(\w+)\s*\([^)]*\)\s*(?:throws\s+\w+\s*)?\{)");
        static const std::regex re_func_kt(R"((?:fun|override fun)\s+(\w+)\s*\()");

        bool in_javadoc  = false;
        bool context_done = false;
        std::string doc_acc;
        std::smatch m;

        while (std::getline(ss, line)) {
            if (!context_done) {
                if (!in_javadoc && line.find("/**") != std::string::npos) {
                    in_javadoc = true;
                    doc_acc += line.substr(line.find("/**") + 3) + " ";
                } else if (in_javadoc) {
                    if (line.find("*/") != std::string::npos) {
                        res.context = doc_acc;
                        context_done = true; in_javadoc = false;
                    } else {
                        auto star = line.find('*');
                        if (star != std::string::npos && star < 4) line = line.substr(star + 1);
                        doc_acc += line + " ";
                    }
                }
            }

            if (std::regex_search(line, m, re_import)) res.imports.push_back(m[1].str());
            if (std::regex_search(line, m, re_pkg))    res.namespaces.push_back(m[1].str());
            if (std::regex_search(line, m, re_class))  res.classes.push_back(m[1].str());
            if (std::regex_search(line, m, re_func_kt)) res.functions.push_back(m[1].str());
            else if (std::regex_search(line, m, re_func)) {
                static const std::vector<std::string> kw = {"if","for","while","switch","catch"};
                std::string fn = m[1].str();
                if (std::find(kw.begin(), kw.end(), fn) == kw.end())
                    res.functions.push_back(fn);
            }
        }

        auto dedup = [](std::vector<std::string>& v) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        dedup(res.imports); dedup(res.namespaces); dedup(res.classes); dedup(res.functions);
        return res;
    }
};

ICMG_REGISTER_EXTRACTOR("java", JavaExtractor);

} // namespace icmg::graph

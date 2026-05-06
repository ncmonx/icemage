#include "base_extractor.hpp"
#include "../../core/registry.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

class PhpExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override {
        return {".php", ".php5", ".phtml"};
    }

    ExtractResult extract(const std::string& /*path*/,
                          const std::string& content) override {
        ExtractResult res;
        std::istringstream ss(content);
        std::string line;

        static const std::regex re_use    (R"(^use\s+([\w\\]+)(?:\s+as\s+\w+)?\s*;)");
        static const std::regex re_require(R"((?:require|include)(?:_once)?\s*[('"]([^'")\s]+))");
        static const std::regex re_class  (R"((?:abstract\s+|final\s+)?(?:class|interface|trait|enum)\s+(\w+))");
        static const std::regex re_func   (R"((?:public|private|protected|static|abstract|\s)*function\s+(\w+)\s*\()");
        static const std::regex re_const  (R"(const\s+(\w+)\s*=)");

        bool in_phpdoc   = false;
        bool context_done = false;
        std::string doc_acc;
        std::smatch m;

        while (std::getline(ss, line)) {
            // PHPDoc context (/** ... */)
            if (!context_done) {
                if (!in_phpdoc && line.find("/**") != std::string::npos) {
                    in_phpdoc = true;
                    doc_acc += line.substr(line.find("/**") + 3) + " ";
                } else if (in_phpdoc) {
                    if (line.find("*/") != std::string::npos) {
                        res.context = doc_acc;
                        context_done = true; in_phpdoc = false;
                    } else {
                        // Strip leading * from PHPDoc lines
                        auto star = line.find('*');
                        if (star != std::string::npos && star < 4) line = line.substr(star + 1);
                        doc_acc += line + " ";
                    }
                }
            }

            if (std::regex_search(line, m, re_use))     res.imports.push_back(m[1].str());
            else if (std::regex_search(line, m, re_require)) res.imports.push_back(m[1].str());
            if (std::regex_search(line, m, re_class))   res.classes.push_back(m[1].str());
            if (std::regex_search(line, m, re_func)) {
                static const std::vector<std::string> kw = {"__construct","__destruct"};
                res.functions.push_back(m[1].str());
            }
        }

        auto dedup = [](std::vector<std::string>& v) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        dedup(res.imports); dedup(res.classes); dedup(res.functions);
        return res;
    }
};

ICMG_REGISTER_EXTRACTOR("php", PhpExtractor);

} // namespace icmg::graph

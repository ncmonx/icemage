#include "base_extractor.hpp"
#include "../../core/registry.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

class CSharpExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override {
        return {".cs", ".csx"};
    }

    ExtractResult extract(const std::string& /*path*/,
                          const std::string& content) override {
        ExtractResult res;
        std::istringstream ss(content);
        std::string line;

        static const std::regex re_using  (R"(^using\s+([\w.]+)\s*;)");
        static const std::regex re_ns     (R"(^namespace\s+([\w.]+))");
        static const std::regex re_class  (R"((?:public|private|protected|internal|abstract|sealed|static|\s)+(?:class|interface|struct|enum|record)\s+(\w+))");
        static const std::regex re_method (R"((?:public|private|protected|internal|static|virtual|override|abstract|async|\s)+(?:[\w<>\[\]?]+\s+)?(\w+)\s*\([^)]*\)\s*(?:where\s+\w+[^{]*)?(?:\{|=>))");
        static const std::regex re_prop   (R"((?:public|private|protected|internal|\s)+[\w<>\[\]?]+\s+(\w+)\s*\{)");

        bool in_xmldoc   = false;
        bool context_done = false;
        std::string doc_acc;
        std::smatch m;

        while (std::getline(ss, line)) {
            // XML doc comment context (///)
            if (!context_done) {
                if (line.find("///") != std::string::npos) {
                    auto pos = line.find("///");
                    std::string part = line.substr(pos + 3);
                    // Strip XML tags
                    std::string clean;
                    bool in_tag = false;
                    for (char c : part) {
                        if (c == '<') { in_tag = true; continue; }
                        if (c == '>') { in_tag = false; continue; }
                        if (!in_tag) clean += c;
                    }
                    doc_acc += clean + " ";
                } else if (!doc_acc.empty()) {
                    res.context = doc_acc;
                    context_done = true;
                }
            }

            if (std::regex_search(line, m, re_using)) res.imports.push_back(m[1].str());
            if (std::regex_search(line, m, re_ns))    res.namespaces.push_back(m[1].str());
            if (std::regex_search(line, m, re_class)) res.classes.push_back(m[1].str());

            // Methods: look for definitions (have { or => body)
            if (line.find('(') != std::string::npos &&
                (line.find('{') != std::string::npos || line.find("=>") != std::string::npos)) {
                if (std::regex_search(line, m, re_method)) {
                    static const std::vector<std::string> kw = {
                        "if","for","foreach","while","switch","catch","using","lock"};
                    std::string fn = m[1].str();
                    if (std::find(kw.begin(), kw.end(), fn) == kw.end() && fn.size() >= 2)
                        res.functions.push_back(fn);
                }
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

ICMG_REGISTER_EXTRACTOR("csharp", CSharpExtractor);

} // namespace icmg::graph

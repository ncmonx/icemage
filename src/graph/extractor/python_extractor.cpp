#include "base_extractor.hpp"
#include "../../core/registry.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

class PythonExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override {
        return {".py", ".pyw"};
    }

    ExtractResult extract(const std::string& /*path*/,
                          const std::string& content) override {
        ExtractResult res;
        std::istringstream ss(content);
        std::string line;

        static const std::regex re_import1(R"(^import\s+([\w.]+))");
        static const std::regex re_import2(R"(^from\s+([\w.]+)\s+import)");
        static const std::regex re_class  (R"(^class\s+(\w+))");
        static const std::regex re_func   (R"(^def\s+(\w+)\s*\()");

        bool in_docstring   = false;
        bool context_done   = false;
        std::string doc_acc;
        char doc_quote = '"';
        std::smatch m;

        while (std::getline(ss, line)) {
            // Docstring context extraction (first """...""")
            if (!context_done) {
                if (!in_docstring) {
                    auto pos = line.find("\"\"\"");
                    auto pos2 = line.find("'''");
                    if (pos != std::string::npos) {
                        in_docstring = true; doc_quote = '"';
                        auto end = line.find("\"\"\"", pos + 3);
                        if (end != std::string::npos) {
                            res.context = line.substr(pos + 3, end - pos - 3);
                            context_done = true; in_docstring = false;
                        } else {
                            doc_acc += line.substr(pos + 3) + " ";
                        }
                    } else if (pos2 != std::string::npos) {
                        in_docstring = true; doc_quote = '\'';
                        doc_acc += line.substr(pos2 + 3) + " ";
                    }
                } else {
                    std::string close = (doc_quote == '"') ? "\"\"\"" : "'''";
                    auto end = line.find(close);
                    if (end != std::string::npos) {
                        doc_acc += line.substr(0, end);
                        res.context = doc_acc;
                        context_done = true; in_docstring = false;
                    } else {
                        doc_acc += line + " ";
                    }
                }
            }

            if (std::regex_search(line, m, re_import1)) res.imports.push_back(m[1].str());
            else if (std::regex_search(line, m, re_import2)) res.imports.push_back(m[1].str());
            if (std::regex_search(line, m, re_class)) res.classes.push_back(m[1].str());
            if (std::regex_search(line, m, re_func))  res.functions.push_back(m[1].str());
        }

        auto dedup = [](std::vector<std::string>& v) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        dedup(res.imports); dedup(res.classes); dedup(res.functions);
        return res;
    }
};

ICMG_REGISTER_EXTRACTOR("python", PythonExtractor);

} // namespace icmg::graph

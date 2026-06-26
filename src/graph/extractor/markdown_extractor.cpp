#include "markdown_extractor.hpp"
#include "../../core/registry.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace icmg::graph {

ExtractResult MarkdownExtractor::extract(const std::string& /*path*/,
                                          const std::string& content) {
    ExtractResult res;
    if (content.empty()) return res;

    std::istringstream ss(content);
    std::string line;

    // [text](target) — standard markdown link
    static const std::regex re_link    (R"re(\[([^\]]*)\]\(([^)]+)\))re");
    // [[wikilink]]
    static const std::regex re_wiki    (R"re(\[\[([^\]]+)\]\])re");
    // heading: # H1 / ## H2 / ### H3
    static const std::regex re_heading (R"re(^(#{1,3})\s+(.+))re");

    while (std::getline(ss, line)) {
        // strip trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // heading -> store in functions (consistent with extractor convention)
        std::smatch m;
        if (std::regex_search(line, m, re_heading)) {
            std::string text = m[2].str();
            // trim trailing whitespace
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                text.pop_back();
            if (!text.empty())
                res.functions.push_back(text);
        }

        // [[wikilinks]] — check before standard links to avoid false overlap
        {
            std::string tmp = line;
            std::smatch wm;
            while (std::regex_search(tmp, wm, re_wiki)) {
                res.imports.push_back("wikilinks:" + wm[1].str());
                tmp = wm.suffix().str();
            }
        }

        // [text](target) links — skip pure anchor (#...) and mailto:
        {
            std::string tmp = line;
            std::smatch lm;
            while (std::regex_search(tmp, lm, re_link)) {
                std::string tgt = lm[2].str();
                if (!tgt.empty() && tgt[0] != '#' &&
                    tgt.compare(0, 7, "mailto:") != 0 &&
                    tgt.compare(0, 7, "http://") != 0 &&
                    tgt.compare(0, 8, "https://") != 0)
                {
                    res.imports.push_back("links:" + tgt);
                }
                tmp = lm.suffix().str();
            }
        }
    }

    // deduplicate
    auto dedup = [](std::vector<std::string>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    dedup(res.imports);
    dedup(res.functions);

    return res;
}

ICMG_REGISTER_EXTRACTOR("markdown", MarkdownExtractor);

} // namespace icmg::graph

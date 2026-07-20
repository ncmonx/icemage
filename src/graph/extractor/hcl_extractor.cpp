#include "hcl_extractor.hpp"
#include "../../core/registry.hpp"
#include <regex>
#include <algorithm>
#include <sstream>

namespace icmg::graph {

namespace {

// Strip HCL comments: line `#` / `//` and block `/* ... */`. Good enough for
// config DDL (does not honor `#` inside strings, rare in practice).
std::string stripComments(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    enum { CODE, LINE, BLOCK } st = CODE;
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        char n = (i + 1 < in.size()) ? in[i + 1] : '\0';
        switch (st) {
            case CODE:
                if (c == '#') { st = LINE; }
                else if (c == '/' && n == '/') { st = LINE; ++i; }
                else if (c == '/' && n == '*') { st = BLOCK; ++i; }
                else out += c;
                break;
            case LINE:
                if (c == '\n') { st = CODE; out += c; }
                break;
            case BLOCK:
                if (c == '*' && n == '/') { st = CODE; ++i; }
                break;
        }
    }
    return out;
}

void pushUnique(std::vector<std::string>& v, const std::string& s) {
    if (s.empty()) return;
    if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

} // namespace

ExtractResult HclExtractor::extract(const std::string& /*path*/,
                                    const std::string& content) {
    ExtractResult res;
    if (content.empty()) return res;

    const std::string hcl = stripComments(content);

    try {
        // Top-level block:  <type> "label" ["label2"] {   OR   <type> {
        // Captures type + up to two quoted labels.
        std::regex re_block(
            R"rx((?:^|\n)\s*([A-Za-z_][A-Za-z0-9_-]*)\s*(?:"([^"]*)")?\s*(?:"([^"]*)")?\s*\{)rx");
        // module "x" { ... source = "..." }  -> we scan source lines globally.
        std::regex re_source(R"rx(source\s*=\s*"([^"]+)")rx");

        static const std::vector<std::string> BLOCK_TYPES = {
            "resource","data","module","variable","output",
            "provider","locals","terraform","import","moved","check"
        };

        for (auto it = std::sregex_iterator(hcl.begin(), hcl.end(), re_block);
             it != std::sregex_iterator(); ++it) {
            std::string type = (*it)[1].str();
            if (std::find(BLOCK_TYPES.begin(), BLOCK_TYPES.end(), type)
                    == BLOCK_TYPES.end())
                continue;  // not a known top-level block -> skip nested/attrs
            std::string l1 = (*it)[2].str();
            std::string l2 = (*it)[3].str();

            std::string cls = type;
            std::string localName;
            if (!l1.empty()) { cls += "." + l1; localName = l1; }
            if (!l2.empty()) { cls += "." + l2; localName = l2; }
            pushUnique(res.classes, cls);
            if (!localName.empty()) pushUnique(res.functions, localName);
        }

        // module sources -> dependency edges.
        for (auto it = std::sregex_iterator(hcl.begin(), hcl.end(), re_source);
             it != std::sregex_iterator(); ++it) {
            pushUnique(res.imports, "module:" + (*it)[1].str());
        }
    } catch (...) {
        // regex failure -> return whatever was collected; never throw.
    }

    return res;
}

ICMG_REGISTER_EXTRACTOR("hcl", HclExtractor);

} // namespace icmg::graph

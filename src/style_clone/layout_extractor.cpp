// v1.22.0 (SC3): regex-based layout extractor implementation.

#include "layout_extractor.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <regex>
#include <sstream>

namespace icmg::style_clone {

namespace {

std::string lowerExt(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

std::vector<std::string> splitWs(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string tok;
    while (is >> tok) if (!tok.empty()) out.push_back(tok);
    return out;
}

// Strip Vue <style> + <script> blocks so we only parse <template>.
std::string isolateVueTemplate(const std::string& src) {
    auto tpos = src.find("<template");
    if (tpos == std::string::npos) return src;  // not Vue SFC; treat whole file
    auto end = src.find("</template>", tpos);
    if (end == std::string::npos) return src.substr(tpos);
    return src.substr(tpos, end + 11 - tpos);
}

// Pull `class="..."` / `:class="..."` / `className="..."` attribute values.
std::vector<std::string> extractClasses(const std::string& tag_open) {
    std::vector<std::string> out;
    static const std::regex re_class(
        R"((?:class|:class|className)\s*=\s*["']([^"']*)["'])",
        std::regex::ECMAScript);
    auto begin = std::sregex_iterator(tag_open.begin(), tag_open.end(), re_class);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        auto val = (*it)[1].str();
        // Strip JSX/Vue expression markers — keep literal class tokens.
        std::string cleaned;
        for (char c : val) cleaned += (c == '{' || c == '}' || c == '`') ? ' ' : c;
        for (auto& tok : splitWs(cleaned)) {
            // Filter ternary / variable refs (anything with non-class chars).
            bool ok = true;
            for (char c : tok) {
                if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_'
                      || c == ':')) { ok = false; break; }
            }
            if (ok && !tok.empty()) out.push_back(tok);
        }
    }
    return out;
}

// Pull attribute names (everything before `=`); strip values.
std::vector<std::string> extractAttrNames(const std::string& tag_open) {
    std::vector<std::string> out;
    static const std::regex re_attr(
        R"(([@:a-zA-Z_][a-zA-Z0-9_\-:.]*)\s*=)",
        std::regex::ECMAScript);
    auto begin = std::sregex_iterator(tag_open.begin(), tag_open.end(), re_attr);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string n = (*it)[1].str();
        if (n == "class" || n == ":class" || n == "className") continue;
        out.push_back(n);
    }
    return out;
}

bool hasBinding(const std::string& tag_open) {
    // Vue: :prop / v-bind / v-on / @click. JSX: prop={...}.
    if (tag_open.find(":") != std::string::npos
     || tag_open.find("v-") != std::string::npos
     || tag_open.find("@") != std::string::npos) return true;
    static const std::regex re_jsx_bind(R"(=\s*\{[^}]+\})", std::regex::ECMAScript);
    return std::regex_search(tag_open, re_jsx_bind);
}

} // namespace

std::string detectLang(const std::string& path) {
    auto ext = lowerExt(path);
    if (ext == "vue")    return "vue";
    if (ext == "jsx")    return "jsx";
    if (ext == "tsx")    return "tsx";
    if (ext == "html" || ext == "htm") return "html";
    if (ext == "svelte") return "svelte";
    return "";  // unknown — caller falls back to generic mode
}

// v1.24.0 (T3): dispatch hook for tree-sitter promotion. Always returns
// false in v1.24.0 — grammar vendoring (vue v0.2.1 + html + svelte source
// drops + CMake static-lib targets) deferred to a follow-up patch within
// the v1.24.x cycle. Public probe so users can detect capability.
// v1.27.0 Phase 3: grammars vendored in-tree under third_party/.
// CMake sets ICMG_HAS_TREESITTER_VUE/HTML/SVELTE when parser.c is found.
extern "C" {
#ifdef ICMG_HAS_TREESITTER_VUE
const void* tree_sitter_vue(void);
#endif
#ifdef ICMG_HAS_TREESITTER_HTML
const void* tree_sitter_html(void);
#endif
#ifdef ICMG_HAS_TREESITTER_SVELTE
const void* tree_sitter_svelte(void);
#endif
}

bool hasTreeSitterGrammar(const std::string& lang) {
#ifdef ICMG_HAS_TREESITTER_VUE
    if (lang == "vue") return tree_sitter_vue() != nullptr;
#endif
#ifdef ICMG_HAS_TREESITTER_HTML
    if (lang == "html") return tree_sitter_html() != nullptr;
#endif
#ifdef ICMG_HAS_TREESITTER_SVELTE
    if (lang == "svelte") return tree_sitter_svelte() != nullptr;
#endif
    (void)lang;
    return false;
}

LayoutTree extractLayout(const std::string& source, const std::string& lang) {
    LayoutTree tree;
    tree.detected_lang = lang.empty() ? "html" : lang;

    // v1.24.0 (T3): tree-sitter dispatch hook. When a grammar is vendored
    // (future patch), this branch will call a tree-sitter walker and bypass
    // the regex scanner below. T4 hash-compat shim must run on the AST
    // node list to keep structural_hash stable across the regex → ts
    // migration (existing v1.22.0 stored patterns remain valid).
    if (hasTreeSitterGrammar(tree.detected_lang)) {
        // Future: dispatch to extractLayoutTS(source, lang).
        // Falls through to regex for now (probe always false in v1.24.0).
    }

    std::string src = (lang == "vue") ? isolateVueTemplate(source) : source;

    // Token scanner: walk tags via regex. ECMAScript regex doesn't support
    // recursion so we do it iteratively with a stack of open nodes.
    static const std::regex re_tag(
        R"(<(/?)([A-Za-z][A-Za-z0-9\-_.]*)([^>]*)(/?)>)",
        std::regex::ECMAScript);

    std::vector<LayoutNode*> stack;
    stack.push_back(&tree.root);
    tree.root.tag = "__root__";

    auto begin = std::sregex_iterator(src.begin(), src.end(), re_tag);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        bool is_close   = (*it)[1].str() == "/";
        std::string tag = (*it)[2].str();
        std::string body= (*it)[3].str();
        bool is_self    = (*it)[4].str() == "/" || tag == "br"
                       || tag == "img" || tag == "hr" || tag == "input";

        if (is_close) {
            if (stack.size() > 1) stack.pop_back();
            continue;
        }

        LayoutNode n;
        n.tag           = tag;
        n.classes       = extractClasses(body);
        n.attr_names    = extractAttrNames(body);
        n.self_closing  = is_self;
        n.has_data_binding = hasBinding(body);
        for (auto& c : n.classes) tree.class_set.insert(c);

        stack.back()->children.push_back(std::move(n));
        ++tree.node_count;
        if (!is_self) {
            stack.push_back(&stack.back()->children.back());
        }
    }
    return tree;
}

static nlohmann::json nodeToJson(const LayoutNode& n) {
    nlohmann::json j;
    j["t"] = n.tag;
    if (!n.classes.empty())    j["c"] = n.classes;
    if (!n.attr_names.empty()) j["a"] = n.attr_names;
    if (n.self_closing)        j["s"] = true;
    if (n.has_data_binding)    j["b"] = true;
    if (!n.children.empty()) {
        nlohmann::json kids = nlohmann::json::array();
        for (const auto& ch : n.children) kids.push_back(nodeToJson(ch));
        j["k"] = std::move(kids);
    }
    return j;
}

static LayoutNode nodeFromJson(const nlohmann::json& j) {
    LayoutNode n;
    if (j.contains("t") && j["t"].is_string()) n.tag = j["t"].get<std::string>();
    if (j.contains("c") && j["c"].is_array())
        for (auto& v : j["c"]) if (v.is_string()) n.classes.push_back(v.get<std::string>());
    if (j.contains("a") && j["a"].is_array())
        for (auto& v : j["a"]) if (v.is_string()) n.attr_names.push_back(v.get<std::string>());
    if (j.contains("s") && j["s"].is_boolean()) n.self_closing = j["s"].get<bool>();
    if (j.contains("b") && j["b"].is_boolean()) n.has_data_binding = j["b"].get<bool>();
    if (j.contains("k") && j["k"].is_array())
        for (auto& v : j["k"]) n.children.push_back(nodeFromJson(v));
    return n;
}

nlohmann::json layoutToJson(const LayoutTree& tree) {
    nlohmann::json j;
    j["lang"]  = tree.detected_lang;
    j["root"]  = nodeToJson(tree.root);
    j["nodes"] = tree.node_count;
    j["classes"] = std::vector<std::string>(tree.class_set.begin(), tree.class_set.end());
    return j;
}

LayoutTree layoutFromJson(const nlohmann::json& j) {
    LayoutTree t;
    if (j.contains("lang") && j["lang"].is_string()) t.detected_lang = j["lang"].get<std::string>();
    if (j.contains("root")) t.root = nodeFromJson(j["root"]);
    if (j.contains("nodes") && j["nodes"].is_number_integer())
        t.node_count = j["nodes"].get<int>();
    if (j.contains("classes") && j["classes"].is_array())
        for (auto& v : j["classes"]) if (v.is_string()) t.class_set.insert(v.get<std::string>());
    return t;
}

static void hashNode(const LayoutNode& n, std::uint64_t& h) {
    auto fnv = [&](const std::string& s) {
        for (char c : s) {
            h ^= (std::uint8_t)c;
            h *= 0x100000001b3ULL;
        }
        h ^= 0x5e;
    };
    fnv(n.tag);
    for (auto& c : n.classes) fnv(c);
    for (auto& ch : n.children) hashNode(ch, h);
}

std::string structuralHash(const LayoutTree& tree) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    hashNode(tree.root, h);
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  (unsigned long long)h);
    return std::string(buf);
}

} // namespace icmg::style_clone

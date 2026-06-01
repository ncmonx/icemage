// v1.22.0 (SC3): layout extractor.
//
// Pulls a compact structural skeleton out of a UI source file (vue/jsx/tsx/
// html/svelte). The output strips literal text + bound expressions so the
// resulting tree captures ONLY presentation (tags, class lists, attribute
// names, nesting). This is what style-clone propagates to other targets.
//
// Implementation note: v1.22.0 ships a regex-based tag scanner rather than
// a tree-sitter integration. Trade-off: cannot tell JSX `{expr}` apart from
// literal text in some edge cases, but for style-clone we ONLY care about
// tag + class + attr-name (no values) so the simpler parser is sufficient.
// A tree-sitter migration is scoped for v1.23+.

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_set>

namespace icmg::style_clone {

struct LayoutNode {
    std::string tag;                          // e.g. "div", "Button"
    std::vector<std::string> classes;         // from class="..." / :class="..."
    std::vector<std::string> attr_names;      // attribute NAMES only (values stripped)
    bool   self_closing = false;
    bool   has_data_binding = false;          // contained {expr} / {{ ... }} / v-bind
    std::vector<LayoutNode> children;
};

struct LayoutTree {
    LayoutNode                   root;        // synthetic root wrapping top-level elements
    std::unordered_set<std::string> class_set;
    int                          node_count = 0;
    std::string                  detected_lang;  // "vue" / "jsx" / "html" / "svelte"
};

// Detect language from extension. Empty string when unknown.
std::string detectLang(const std::string& path);

// Extract layout from source text. `lang` may be empty — fall back to detection
// via filename (caller passes path) or generic mode (treat as HTML-ish).
LayoutTree extractLayout(const std::string& source, const std::string& lang);

// Serialize to compact JSON for DB storage.
nlohmann::json layoutToJson(const LayoutTree& tree);

// Reverse — parse stored JSON back into in-memory tree.
LayoutTree layoutFromJson(const nlohmann::json& j);

// 16-hex-char structural hash (FNV-1a over tag+class names; ignores attr names
// + has_data_binding so the hash is stable across data-only edits).
std::string structuralHash(const LayoutTree& tree);

// v1.24.0 (T3): tree-sitter availability probe. Returns true when grammar
// source for `lang` is present in third_party/tree-sitter-<lang>/src/ and
// the build was compiled with ICMG_USE_TREESITTER_<LANG>=ON. Always false
// in v1.24.0 ship — grammar source vendoring deferred to a follow-up patch
// (vue v0.2.1 + html + svelte). The dispatch hook in extractLayout() will
// auto-promote to tree-sitter once this probe returns true.
bool hasTreeSitterGrammar(const std::string& lang);

} // namespace icmg::style_clone

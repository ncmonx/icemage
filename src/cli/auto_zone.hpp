// Phase 72 T9: heuristic zone inference from prompt keywords.
//
// Maps common task verbs/nouns → zone names. Used by recall/pack to auto-set
// `--zone X` when user didn't specify, sharpening BM25 IDF without manual
// flag overhead. Conservative: only fires on strong signals; falls back to
// no-zone (all-corpus) on ambiguous prompts.

#pragma once
#include <string>
#include <vector>

namespace icmg::cli {

// Returns inferred zone name, or empty string when no strong match.
inline std::string inferZone(const std::string& prompt) {
    // Lower-case copy for case-insensitive match.
    std::string lower;
    lower.reserve(prompt.size());
    for (char c : prompt) lower += (char)::tolower((unsigned char)c);

    struct Rule { const char* zone; std::vector<const char*> keywords; };
    static const std::vector<Rule> rules = {
        {"auth",     {"login", "logout", "jwt", "token", "session", "oauth",
                      "password", "auth ", "signin", "signup", "credentials"}},
        {"db",       {"migration", "schema", "sql ", "query", "index ",
                      "table ", "rollback", "sqlite", "postgres", "mysql"}},
        {"graph",    {"graph", "scanner", "extractor", "tree-sitter",
                      "ast ", "symbol", "edge", "node graph"}},
        {"imem",     {"memory recall", "bm25", "embedding", "consolidate",
                      "memoir", "decay"}},
        {"tkil",     {"tkil", "filter ", "command output", "noisy", "shrink"}},
        {"mcp",      {"mcp", "tools/list", "tool call", "anthropic batch"}},
        {"ui",       {"react", "tsx ", "component", "frontend", "css ",
                      "html ", "browser"}},
        {"cli",      {"command line", "cli ", "subcommand", "argv",
                      "dispatcher"}},
        {"hooks",    {"hook ", "pretooluse", "posttooluse", "sessionstart",
                      "userpromptsubmit", "settings.local.json"}},
        {"compress", {"compress", "glossary", "token reduce"}},
    };

    for (auto& r : rules) {
        for (auto* kw : r.keywords) {
            if (lower.find(kw) != std::string::npos) return r.zone;
        }
    }
    return "";
}

} // namespace icmg::cli

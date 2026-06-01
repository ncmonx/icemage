#include "symbol_resolver.hpp"
#include "db.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace icmg::core {

namespace {

// Strip a single leading '@' if present.
std::string stripAt(const std::string& token) {
    if (!token.empty() && token[0] == '@') {
        return token.substr(1);
    }
    return token;
}

// Lowercase helper for case-insensitive basename comparison.
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

// Extract basename without extension from a path string.
std::string baseStem(const std::string& path) {
    namespace fs = std::filesystem;
    fs::path p(path);
    return p.stem().string();
}

// --- SKILL lookup ---
// Checks context_nodes where node_key starts with "skills/" OR exactly matches
// "skills/<token>" or node_key == token. Returns best match or empty value.
SymbolMention trySkill(const std::string& bare, Db& db) {
    SymbolMention result;

    // Exact node_key match first (handles "@skills/auth/login").
    bool found = false;
    db.query(
        "SELECT node_key FROM context_nodes "
        "WHERE active=1 AND node_key=? LIMIT 1",
        {bare},
        [&](const Row& row) {
            if (!found && !row.empty()) {
                result.kind  = SymbolKind::SKILL;
                result.value = row[0];
                result.score = 1.0;
                found = true;
            }
        }
    );
    if (found) return result;

    // Prefix "skills/" match: node_key starts with "skills/" and the stem
    // (last path component without extension) matches bare token.
    std::string lower_bare = toLower(bare);
    db.query(
        "SELECT node_key FROM context_nodes "
        "WHERE active=1 AND node_key LIKE 'skills/%' ORDER BY node_key",
        {},
        [&](const Row& row) {
            if (found || row.empty()) return;
            const std::string& key = row[0];
            // Check if any path segment equals the bare token (case-insensitive).
            std::filesystem::path p(key);
            for (auto it = p.begin(); it != p.end(); ++it) {
                if (toLower(it->string()) == lower_bare) {
                    result.kind  = SymbolKind::SKILL;
                    result.value = key;
                    result.score = 0.9;
                    found = true;
                    return;
                }
            }
        }
    );
    return result;
}

// --- SYMBOL lookup ---
// Checks graph_nodes.symbol_name (Phase 18 two-tier graph).
// Returns UNKNOWN if no symbol table or no match.
SymbolMention trySymbol(const std::string& bare, Db& db) {
    SymbolMention result;
    // TODO: If symbol_name column is absent (pre-Phase-18 DB), this query will
    // fail. Wrapped in try/catch to degrade gracefully.
    try {
        int count = 0;
        std::string matched;
        db.query(
            "SELECT symbol_name FROM graph_nodes "
            "WHERE symbol_name=? AND symbol_name IS NOT NULL LIMIT 2",
            {bare},
            [&](const Row& row) {
                ++count;
                if (count == 1 && !row.empty()) matched = row[0];
            }
        );
        // Only return a match when exactly one row matches (unique).
        if (count == 1 && !matched.empty()) {
            result.kind  = SymbolKind::SYMBOL;
            result.value = matched;
            result.score = 1.0;
        }
    } catch (...) {
        // symbol_name column absent — skip SYMBOL lookup.
    }
    return result;
}

// --- FILE lookup ---
// Checks graph_nodes.path: unique basename (stem) match.
SymbolMention tryFile(const std::string& bare, Db& db) {
    SymbolMention result;
    std::string lower_bare = toLower(bare);

    int count = 0;
    std::string matched_path;
    try {
        db.query(
            "SELECT path FROM graph_nodes WHERE kind='file' OR kind IS NULL OR kind=''",
            {},
            [&](const Row& row) {
                if (row.empty()) return;
                const std::string& path = row[0];
                std::string stem = toLower(baseStem(path));
                if (stem == lower_bare) {
                    ++count;
                    if (count == 1) matched_path = path;
                }
            }
        );
    } catch (...) {
        return result;
    }

    if (count == 1 && !matched_path.empty()) {
        result.kind  = SymbolKind::FILE;
        result.value = matched_path;
        result.score = 1.0;
    }
    return result;
}

} // anonymous namespace

SymbolMention resolveSymbolMention(const std::string& token, Db& db) {
    const std::string bare = stripAt(token);
    if (bare.empty()) {
        return SymbolMention{};
    }

    // Priority 1: SKILL
    {
        auto r = trySkill(bare, db);
        if (r.kind != SymbolKind::UNKNOWN) return r;
    }

    // Priority 2: SYMBOL (graph_nodes.symbol_name, Phase 18)
    {
        auto r = trySymbol(bare, db);
        if (r.kind != SymbolKind::UNKNOWN) return r;
    }

    // Priority 3: FILE (graph_nodes.path basename)
    {
        auto r = tryFile(bare, db);
        if (r.kind != SymbolKind::UNKNOWN) return r;
    }

    return SymbolMention{};  // UNKNOWN
}

} // namespace icmg::core

// 2026-06-06: pure helpers for moments-in-persona (#moments). No DB/CLI/IO.
// Identity-agnostic: callers pass core::currentUser(); never hardcode an identity here.
#pragma once
#include <string>
#include <vector>
#include <cctype>
#include <cstdint>
#include "memory_node.hpp"
#include "../core/profile_store.hpp"

namespace icmg::imem {

// Lowercase; runs of non-alnum -> single '-'; trim '-'; empty -> "moment".
inline std::string momentSlug(const std::string& title) {
    std::string s; bool dash = false;
    for (char c : title) {
        if (std::isalnum((unsigned char)c)) { s += (char)std::tolower((unsigned char)c); dash = false; }
        else if (!s.empty() && !dash) { s += '-'; dash = true; }
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s.empty() ? "moment" : s;
}

// Heuristic (tightened 2026-06-06 after dry-run found false-positives): a moment is a
// memoir/decision whose TOPIC is relationship-flavored. Match the allowlist in the TOPIC
// (NOT content — content substrings like "persona"/"identity" appear in technical nodes too),
// and EXCLUDE technical memoirs/decisions by topic keyword. Non-technical memoirs count as
// moments (they are reflective/personal); decisions only with a relationship word in topic.
inline bool isRelationshipMoment(const std::string& topic, const std::string& /*content*/,
                                 const std::vector<std::string>& allow) {
    auto lower = [](std::string x){ for (auto& c : x) c = (char)std::tolower((unsigned char)c); return x; };
    std::string t = lower(topic);
    bool is_memoir   = t.rfind("memoir:", 0) == 0;
    bool is_decision = t.rfind("decisions-", 0) == 0;
    if (!is_memoir && !is_decision) return false;
    // Technical topics are never relationship moments (excluded by topic keyword).
    static const char* kExclude[] = {
        "release", "changelog", "history", "agent", "llm", "compact", "consolidate",
        "token", "premium", "routing", "graph", "build", "gate", "hookio", "lint",
        "migration", "schema", "vulkan"
        // NOTE: do NOT add short substrings like "ci" — it matches "de-ci-sions".
    };
    for (auto* ex : kExclude) if (t.find(ex) != std::string::npos) return false;
    if (is_memoir) return true;                       // non-technical memoir = a moment
    for (auto& a : allow) if (t.find(lower(a)) != std::string::npos) return true;  // decisions- w/ relationship word
    return false;
}

// Curated migration matcher: explicit topic substrings, case-insensitive. Empty list => no
// match (safe: a `migrate --topic` with no topics migrates nothing). Empty substrings skipped.
// Used by `icmg moment migrate --topic <s>` to bypass the (rejected) auto-heuristic.
inline bool topicMatchesAny(const std::string& topic, const std::vector<std::string>& subs) {
    if (subs.empty()) return false;
    auto lower = [](std::string x){ for (auto& c : x) c = (char)std::tolower((unsigned char)c); return x; };
    std::string t = lower(topic);
    for (auto& s : subs) { if (!s.empty() && t.find(lower(s)) != std::string::npos) return true; }
    return false;
}

// FNV-1a 64-bit hex — stable content fingerprint for idempotent sync.
inline std::string contentHash(const std::string& s) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char ch : s) { h ^= ch; h *= 1099511628211ULL; }
    static const char* hex = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[i] = hex[h & 0xF]; h >>= 4; }
    return out;
}

// Map a persona ProfileRow (_moments) into a MemoryNode so recall can merge + print it.
inline MemoryNode profileRowToNode(const icmg::core::ProfileRow& r) {
    MemoryNode n;
    n.topic    = "moment:" + r.key;
    n.content  = r.content;
    n.keywords = "moment persona " + r.zone;
    n.importance = 2;          // moments are high-importance by nature
    n.zone     = r.zone;
    return n;
}

// Sync line: "<key>\t<content-with-\n-escaped>". Tabs in content dropped.
inline std::string momentSyncLine(const std::string& key, const std::string& content) {
    std::string c; for (char ch : content) { if (ch=='\n'){c+="\\n";continue;} if(ch=='\t')continue; c+=ch; }
    return key + "\t" + c;
}
inline bool parseMomentSyncLine(const std::string& line, std::string& key, std::string& content) {
    auto tab = line.find('\t'); if (tab == std::string::npos) return false;
    key = line.substr(0, tab); content.clear();
    std::string raw = line.substr(tab + 1);
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size() && raw[i+1] == 'n') { content += '\n'; ++i; }
        else content += raw[i];
    }
    return !key.empty();
}

} // namespace icmg::imem

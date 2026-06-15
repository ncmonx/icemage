#pragma once
// Progressive-disclosure recall: Layer-1 index helpers.
// Spec: docs/2026-06-15-recall-progressive-disclosure.md (2026-06-15)
// Riset asal: thedotmack/claude-mem progressive-disclosure (decisions-research).
//
// Pure functions (no DB, no I/O) so the index format, typed-icon mapping, and
// semantic-title derivation are unit-testable in isolation -- same pattern as
// recall_json.hpp. The command layer (recall_cmd.cpp) calls these to render the
// `--index` view; `--get` reuses MemoryStore::get for full detail.

#include "../imem/memory_node.hpp"
#include "../core/token_budget.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace icmg::cli {

// ---- typed icon (Option A: zero-migration, derive from topic + importance) --
// 9-type legend mirrors claude-mem so the vocabulary is familiar:
//   🎯 session-goal  🔴 gotcha  🟡 problem-solution  🔵 how-it-works
//   🟢 what-changed  🟣 discovery  🟠 why-it-exists  🟤 decision  ⚖️ trade-off
// v1 surfaces the subset we can infer without a stored `type` column.
inline const char* iconFor(const imem::MemoryNode& n) {
    std::string t = n.topic;
    std::transform(t.begin(), t.end(), t.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    // critical importance dominates -> gotcha (most attention-worthy)
    if (n.importance >= 3)                          return "\xF0\x9F\x94\xB4"; // 🔴
    // research / discovery before the generic decisions- prefix
    if (t.find("research") != std::string::npos ||
        t.find("riset")    != std::string::npos)    return "\xF0\x9F\x9F\xA3"; // 🟣
    if (t.find("fix")  != std::string::npos ||
        t.find("bug")  != std::string::npos)        return "\xF0\x9F\x9F\xA1"; // 🟡
    if (t.rfind("decisions", 0) == 0 ||
        t.find("decision") != std::string::npos)    return "\xF0\x9F\x9F\xA4"; // 🟤
    if (t.rfind("plan", 0) == 0)                    return "\xF0\x9F\x8E\xAF"; // 🎯
    return "\xF0\x9F\x94\xB5"; // 🔵 how-it-works (default)
}

// ---- semantic title: first clause/sentence of content, <= max_words / chars --
inline std::string makeTitle(const std::string& content,
                             size_t max_chars = 64, size_t max_words = 10) {
    // take up to the first newline / sentence end, collapse whitespace.
    std::string s;
    s.reserve(content.size());
    for (char c : content) {
        if (c == '\n' || c == '\r') break;     // first line only
        s.push_back(c);
    }
    // collapse runs of whitespace to single spaces, trim leading.
    std::string collapsed; bool sp = true;
    for (char c : s) {
        if (std::isspace((unsigned char)c)) { if (!sp) { collapsed.push_back(' '); sp = true; } }
        else { collapsed.push_back(c); sp = false; }
    }
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    // word cap
    std::string out; size_t words = 0; bool inw = false;
    for (char c : collapsed) {
        if (c == ' ') {
            if (inw) { ++words; inw = false; if (words >= max_words) break; }
            out.push_back(c);
        } else { inw = true; out.push_back(c); }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    // char cap
    if (out.size() > max_chars) {
        out.resize(max_chars);
        while (!out.empty() && out.back() == ' ') out.pop_back();
    }
    return out;
}

// ---- one index row: "#ID  <icon>  <title>  ~<tok>" -------------------------
inline std::string formatIndexLine(const imem::MemoryNode& n) {
    std::ostringstream os;
    os << "#" << n.id << " " << iconFor(n) << " "
       << makeTitle(n.content)
       << "  ~" << core::estimateTokens(n.content);
    return os.str();
}

// ---- full index, grouped by `by` (topic|date|file). v1: topic|other -------
inline std::string formatIndex(const std::vector<imem::MemoryNode>& nodes,
                               const std::string& by = "topic") {
    if (nodes.empty()) return std::string("No results.\n");
    std::ostringstream os;
    // stable group order = first-seen; one header per group, rows indented.
    std::vector<std::string> order;
    for (auto& n : nodes) {
        const std::string key = (by == "topic") ? n.topic : n.topic; // v1: topic only
        if (std::find(order.begin(), order.end(), key) == order.end())
            order.push_back(key);
    }
    int64_t total_tok = 0;
    for (auto& n : nodes) total_tok += core::estimateTokens(n.content);
    os << nodes.size() << " hit(s) (~" << total_tok << " tok index)\n\n";
    for (auto& key : order) {
        os << key << "\n";
        for (auto& n : nodes) {
            const std::string nk = (by == "topic") ? n.topic : n.topic;
            if (nk != key) continue;
            os << "  " << formatIndexLine(n) << "\n";
        }
    }
    os << "\n\xF0\x9F\x92\xA1 fetch detail: icmg recall --get <id>[,<id>...]"
          "   |   critical types (\xF0\x9F\x94\xB4\xF0\x9F\x9F\xA4\xE2\x9A\x96\xEF\xB8\x8F) often worth fetching now\n";
    return os.str();
}

} // namespace icmg::cli

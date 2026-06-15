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
#include <ctime>
#include <cstdio>

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

// ---- timeline view: chronological, grouped by day --------------------------
// Day bucket key "YYYY-MM-DD" derived from a unix epoch in UTC. UTC (not local)
// keeps the format deterministic across machines/timezones -> unit-testable.
inline std::string dayKey(int64_t epoch) {
    if (epoch <= 0) return "unknown";
    std::time_t t = (std::time_t)epoch;
    std::tm tmv{};
#if defined(_WIN32)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return std::string(buf);
}

// HH:MM (UTC) for one timeline row.
inline std::string hourMin(int64_t epoch) {
    if (epoch <= 0) return "--:--";
    std::time_t t = (std::time_t)epoch;
    std::tm tmv{};
#if defined(_WIN32)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return std::string(buf);
}

// Chronological index: sort by created_at DESC (newest first), print one day
// header per bucket, rows = "HH:MM <icon> <title>  #id". Same typed-icon + title
// helpers as the topic index so the vocabulary stays consistent.
inline std::string formatTimeline(std::vector<imem::MemoryNode> nodes) {
    if (nodes.empty()) return std::string("No results.\n");
    std::sort(nodes.begin(), nodes.end(),
              [](const imem::MemoryNode& a, const imem::MemoryNode& b){
                  return a.created_at > b.created_at;
              });
    std::ostringstream os;
    int64_t total_tok = 0;
    for (auto& n : nodes) total_tok += core::estimateTokens(n.content);
    os << nodes.size() << " hit(s) timeline, newest first (~" << total_tok << " tok index)\n\n";
    std::string curDay;
    for (auto& n : nodes) {
        std::string d = dayKey(n.created_at);
        if (d != curDay) { os << d << "\n"; curDay = d; }
        os << "  " << hourMin(n.created_at) << " " << iconFor(n) << " "
           << makeTitle(n.content) << "  #" << n.id << "\n";
    }
    os << "\n\xF0\x9F\x92\xA1 fetch detail: icmg recall --get <id>[,<id>...]\n";
    return os.str();
}

// ---- citation header: "[score] #id  <icon> topic" for default recall -------
// Default recall output omitted the node id, so results could not be cited or
// re-fetched with `--get`. This pure helper renders a citable header line; the
// command layer appends the score-formatted content beneath it.
inline std::string formatCitationHeader(const imem::MemoryNode& n,
                                        const std::string& scoreStr) {
    std::ostringstream os;
    os << "[" << scoreStr << "] #" << n.id << " " << iconFor(n) << " " << n.topic;
    return os.str();
}

} // namespace icmg::cli

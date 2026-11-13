#pragma once
// v2.30.0 Model-AI ref flow -- pure policy (no DB / IO), unit-tested in
// tests/cli/test_ref_flow.cpp. Spec: docs/plans/2026-11-13-model-ai-ref-flow.md
//
//   Problem in project A -> look up refs in the Model-AI DB (exe-dir persona DB)
//   and in project A -> gate "Ada Ref / Solusi?" -> if not, look in projects B/C
//   (registry, filtered by isShareableProject) -> work -> verified result is
//   written back to the Model-AI DB (+ project A).
//
// All tunables live here, in one place.

#include "private_filter.hpp"
#include "../core/cross_turn_dedup.hpp"
#include "../imem/recall_confidence.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace icmg::cli::refflow {

// ---- tunables --------------------------------------------------------------
constexpr int    kRefWritebackMaxPerSession = 3;     // auto write-backs per session
constexpr size_t kRefProblemCap             = 400;   // bytes kept of the problem text
constexpr size_t kRefSolutionCap            = 800;   // bytes kept of the solution text
constexpr size_t kRefMaxFilesListed         = 12;    // files named in a solution
constexpr double kRefDedupJaccard           = 0.85;  // problem near-dup threshold
constexpr int    kRefLookupLimit            = 3;     // Model-AI refs injected per prompt
constexpr int    kCrossProjectBudgetMs      = 800;   // wall-clock budget for B/C lookup
constexpr int    kCrossProjectMaxHits       = 3;     // B/C hits injected per prompt
constexpr int    kCrossProjectPerProject    = 2;     // hits fetched per B/C project
constexpr const char* kRefWritebackEnv      = "ICMG_REF_WRITEBACK";
constexpr const char* kRefLookupEnv         = "ICMG_REF_LOOKUP";   // 0/off disables the Model-AI lookup
// A Model-AI ref counts as found -- and satisfies the gate -- when the prompt
// covers at least this share of the ref's problem words (stopwords dropped)
// and at least kRefMinMatched of them. Raw -bm25 is NOT used: on a small ref
// corpus its IDF collapses to ~0, so an absolute floor would never fire.
constexpr double kRefMinCoverage            = 0.5;
constexpr size_t kRefMinMatched             = 2;

// ---- path helpers ----------------------------------------------------------
// Lowercase, '\\' -> '/', no trailing '/'. Comparison form only.
inline std::string normPathKey(const std::string& p) {
    std::string s;
    s.reserve(p.size());
    for (char ch : p) {
        char c = (ch == '\\') ? '/' : ch;
        s += (char)std::tolower((unsigned char)c);
    }
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

inline std::vector<std::string> pathSegments(const std::string& norm) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : norm) {
        if (c == '/') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// T1: may project `path` be searched as a B/C reference source?
// Rejects scratch/system/home/build dirs and the current project.
inline bool isShareableProject(const std::string& path,
                               const std::string& home,
                               const std::string& cwd) {
    const std::string p = normPathKey(path);
    if (p.empty()) return false;
    if (!home.empty() && p == normPathKey(home)) return false;
    if (!cwd.empty()  && p == normPathKey(cwd))  return false;
    if (p.find("/windows/system32") != std::string::npos) return false;
    for (const auto& seg : pathSegments(p)) {
        if (seg == "temp" || seg == "tmp") return false;
        if (seg == "build" || seg.rfind("build-", 0) == 0) return false;
    }
    return true;
}

// ---- T2 gate "Ada Ref / Solusi?" -------------------------------------------
// Satisfied (stop, no B/C lookup) when the best hit is at least `med`.
inline bool refGateSatisfied(double best_score, double min_score) {
    if (best_score <= 0.0 || best_score < min_score) return false;
    return std::strcmp(imem::confidenceTier(best_score, min_score), "low") != 0;
}

// Words too common to say anything about a problem (one list, one place).
inline bool isRefStopword(const std::string& w) {
    static const char* const kStop[] = {
        "the", "an", "and", "or", "of", "to", "in", "on", "at", "for", "with", "is",
        "it", "be", "by", "as", "if", "when", "this", "that", "from", "not", "no",
        "yg", "di", "ke", "dan", "ini", "itu", "ada", "nya", "aja", "ya", "dong"};
    for (auto* s : kStop) if (w == s) return true;
    return false;
}

// Share of the ref problem's content words that appear in the prompt, or 0
// when fewer than kRefMinMatched words match.
inline double refCoverage(const std::string& prompt, const std::string& problem) {
    const auto q = core::wordSet(prompt);
    size_t total = 0, hit = 0;
    for (const auto& w : core::wordSet(problem)) {
        if (isRefStopword(w)) continue;
        ++total;
        if (q.count(w)) ++hit;
    }
    if (total == 0 || hit < kRefMinMatched) return 0.0;
    return (double)hit / (double)total;
}

// Gate over both sources: a Model-AI ref with coverage >= kRefMinCoverage, or
// a local project-A hit of at least `med` tier, means "Ada Ref" -> skip B/C.
inline bool refFlowGate(double best_ref_coverage, double best_local_score, double min_score) {
    return best_ref_coverage >= kRefMinCoverage || refGateSatisfied(best_local_score, min_score);
}

// ---- T4 write-back policy --------------------------------------------------
struct WritebackSignals {
    bool disabled            = false;  // kill switch
    bool changed             = false;  // the task produced edits / a diff
    bool verified_ok         = false;  // a verification with exit 0 in the window
    bool verify_failed_after = false;  // latest verification failed -> not done
    bool ref_reused          = false;  // an injected ref:/x: hit was observed reused
    bool duplicate           = false;  // near-dup ref already stored
    int  stored_this_session = 0;
};

enum class WritebackDecision {
    Store, SkipDisabled, SkipNoChange, SkipUnverified, SkipDuplicate, SkipCap
};

inline const char* writebackDecisionName(WritebackDecision d) {
    switch (d) {
        case WritebackDecision::Store:          return "store";
        case WritebackDecision::SkipDisabled:   return "skip-disabled";
        case WritebackDecision::SkipNoChange:   return "skip-no-change";
        case WritebackDecision::SkipUnverified: return "skip-unverified";
        case WritebackDecision::SkipDuplicate:  return "skip-duplicate";
        case WritebackDecision::SkipCap:        return "skip-cap";
    }
    return "?";
}

inline WritebackDecision decideWriteback(const WritebackSignals& s) {
    if (s.disabled) return WritebackDecision::SkipDisabled;
    if (!s.changed) return WritebackDecision::SkipNoChange;
    if (s.verify_failed_after) return WritebackDecision::SkipUnverified;
    if (!s.verified_ok && !s.ref_reused) return WritebackDecision::SkipUnverified;
    if (s.duplicate) return WritebackDecision::SkipDuplicate;
    if (s.stored_this_session >= kRefWritebackMaxPerSession) return WritebackDecision::SkipCap;
    return WritebackDecision::Store;
}

// ICMG_REF_WRITEBACK=0|off|false|no disables auto write-back; unset = on.
inline bool writebackDisabledByEnv(const char* v) {
    if (!v) return false;
    std::string s = normPathKey(v);
    return s == "0" || s == "off" || s == "false" || s == "no";
}

// ---- T4 content builder ----------------------------------------------------
struct RefContent {
    std::string problem;
    std::string solution;
};

inline std::string capUtf8(const std::string& s, size_t cap) {
    if (s.size() <= cap) return s;
    size_t n = cap;
    // never cut inside a UTF-8 sequence
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) --n;
    return s.substr(0, n);
}

inline std::string collapseWs(const std::string& s) {
    std::string out;
    bool sp = false;
    for (char c : s) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') { sp = !out.empty(); continue; }
        if (sp) { out += ' '; sp = false; }
        out += c;
    }
    return out;
}

inline RefContent buildRefContent(const std::string& prompt,
                                  const std::vector<std::string>& files,
                                  const std::string& verify_cmd,
                                  const std::string& outcome) {
    RefContent c;
    c.problem = capUtf8(collapseWs(stripPrivate(prompt)), kRefProblemCap);
    std::string sol;
    std::string out = collapseWs(stripPrivate(outcome));
    if (!out.empty()) sol += out;
    if (!files.empty()) {
        if (!sol.empty()) sol += " | ";
        sol += "files: ";
        size_t n = std::min(files.size(), kRefMaxFilesListed);
        for (size_t i = 0; i < n; ++i) { if (i) sol += ", "; sol += files[i]; }
        if (files.size() > n) sol += " (+" + std::to_string(files.size() - n) + " more)";
    }
    if (!verify_cmd.empty()) {
        if (!sol.empty()) sol += " | ";
        sol += "verified: " + collapseWs(stripPrivate(verify_cmd));
    }
    c.solution = capUtf8(sol, kRefSolutionCap);
    return c;
}

}  // namespace icmg::cli::refflow

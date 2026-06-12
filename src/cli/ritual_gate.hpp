#pragma once
// Post-change ritual gate — POSITIVE-side enforcement (2026-06-11).
//
// The defensive enforcement (`strict` / `rule-eval` / `rule-daemon`) blocks
// NATIVE calls (Read/Grep/Bash) and redirects them to icmg — i.e. it enforces
// "do NOT use native". This is its missing *positive* counterpart: it enforces
// "you MUST actually USE the icmg sync features after a change".
//
// The icmg workflow convention (see AGENTS.md) mandates a 5-sync ritual after
// every change (graph update / store / zone / wflog / verify), but that was a
// SOFT instruction the model could skip
// — exactly the failure recorded in icmg memory:
//     "fail: icmg-first hook enforcement — soft PreToolUse additional-context
//      reminder ... Failed because: Model (any) can ignore appended reminders."
// This gate makes the ritual checkable (and, via the Stop hook, blockable).
//
// PURE CORE — no I/O — so the verdict logic is unit-testable. The `icmg ritual`
// command wraps it with a marker file; the Stop hook calls `icmg ritual gate`.

#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace icmg::cli {

// The five sync steps mandated after a code change.
enum class RitualStep { Graph, Store, Zone, Wflog, Verify };

inline std::string ritualStepName(RitualStep s) {
    switch (s) {
        case RitualStep::Graph:  return "graph-update";
        case RitualStep::Store:  return "store";
        case RitualStep::Zone:   return "zone";
        case RitualStep::Wflog:  return "wflog";
        case RitualStep::Verify: return "verify";
    }
    return "?";
}

struct RitualState {
    bool changed = false;             // an Edit/Write happened since last clear
    std::set<RitualStep> done;        // steps observed since the change
};

struct RitualVerdict {
    bool owed = false;                // changed && a REQUIRED step is missing
    std::vector<RitualStep> missing;  // required steps not yet done (set order)
};

// REQUIRED = {Store, Wflog}: the two steps that need the model's JUDGMENT and
// are NOT automated — the decision/why (store) and the step narrative (wflog)
// can't be auto-derived. Graph is intentionally NOT required: the edit hook
// queues touched files and the Stop hook already fires `icmg graph scan` once
// per turn (src/core/hooks/internals.cpp ~L1028), so the graph auto-refreshes —
// demanding a manual `graph update` would be redundant and cause false blocks.
// Graph + Zone + Verify are ADVISORY (tracked + surfaced, never cause `owed`).
inline const std::set<RitualStep>& ritualRequired() {
    static const std::set<RitualStep> req{RitualStep::Store, RitualStep::Wflog};
    return req;
}

inline RitualVerdict evaluateRitual(const RitualState& s) {
    RitualVerdict v;
    if (!s.changed) return v;          // nothing owed if no change happened
    for (RitualStep r : ritualRequired()) {
        if (s.done.find(r) == s.done.end()) v.missing.push_back(r);
    }
    v.owed = !v.missing.empty();
    return v;
}

// Map an invoked icmg subcommand (sub = argv0 subcommand, arg1 = its first arg)
// to the ritual step it satisfies. Returns nullopt for ritual-neutral commands.
// `store` has several equivalent forms (store/memoir/known-issue/quick-store),
// any of which satisfies the Store step.
inline std::optional<RitualStep> stepForCommand(const std::string& sub,
                                                 const std::string& arg1) {
    if (sub == "graph" && arg1 == "update") return RitualStep::Graph;
    if (sub == "graph-update")              return RitualStep::Graph;  // alias form
    if (sub == "store" || sub == "memoir" || sub == "known-issue" ||
        sub == "quick-store")               return RitualStep::Store;
    if (sub == "wflog")                     return RitualStep::Wflog;
    if (sub == "verify")                    return RitualStep::Verify;
    if (sub == "zone")                      return RitualStep::Zone;
    return std::nullopt;
}

// Parse a full shell command LINE and return the sync steps it contains, in
// first-seen order (deduped). Fixes the recorder bug (known-issue #33370) where
// the Bash hook only inspected the first token of a command that started
// literally with `icmg `. This handles two real defeats:
//   (a) leading env-var prefixes  — `RAW=1 icmg store ...`  (strip VAR=val)
//   (b) chained / piped commands  — `icmg store ... && icmg wflog ...`
//                                    (scan every &&/||/;/| segment)
// Each segment's first word is basename-matched against `icmg`/`icmg.exe`; its
// next two tokens feed stepForCommand. Pure (no I/O) so it is unit-testable.
inline std::vector<RitualStep> parseSyncStepsFromCommandLine(const std::string& line) {
    std::vector<RitualStep> out;
    std::set<RitualStep> seen;

    auto consider = [&](const std::string& raw) {
        // 1. trim leading whitespace
        size_t a = 0;
        while (a < raw.size() && (raw[a] == ' ' || raw[a] == '\t')) ++a;
        std::string s = raw.substr(a);
        // 2. strip leading VAR=val env prefixes (one or more): NAME=...<space>
        for (;;) {
            size_t i = 0;
            if (i >= s.size() ||
                !(std::isalpha((unsigned char)s[i]) || s[i] == '_')) break;
            size_t j = i + 1;
            while (j < s.size() &&
                   (std::isalnum((unsigned char)s[j]) || s[j] == '_')) ++j;
            if (j >= s.size() || s[j] != '=') break;       // not a VAR= token
            size_t k = j + 1;                              // consume value...
            while (k < s.size() && s[k] != ' ' && s[k] != '\t') ++k;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            if (k == 0) break;
            s = s.substr(k);
        }
        // 3. take up to three whitespace tokens (prog, sub, arg1)
        std::vector<std::string> tok;
        size_t i = 0;
        while (i < s.size() && tok.size() < 3) {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
            size_t st = i;
            while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
            if (i > st) tok.push_back(s.substr(st, i - st));
        }
        if (tok.empty()) return;
        // 4. basename(prog) must be icmg (allow path + .exe on Windows)
        std::string prog = tok[0];
        size_t slash = prog.find_last_of("/\\");
        if (slash != std::string::npos) prog = prog.substr(slash + 1);
        if (prog.size() > 4 && prog.compare(prog.size() - 4, 4, ".exe") == 0)
            prog = prog.substr(0, prog.size() - 4);
        if (prog != "icmg") return;
        std::string sub  = tok.size() > 1 ? tok[1] : "";
        std::string arg1 = tok.size() > 2 ? tok[2] : "";
        if (auto step = stepForCommand(sub, arg1))
            if (seen.insert(*step).second) out.push_back(*step);
    };

    // Split on shell separators. A single '&'/'|' also covers '&&'/'||'
    // (the empty middle segment is harmless). '2>&1' splits too, but the
    // leading `icmg <sub>` of that segment is still intact -> step detected.
    std::string seg;
    for (char c : line) {
        if (c == ';' || c == '\n' || c == '|' || c == '&') {
            consider(seg);
            seg.clear();
        } else {
            seg += c;
        }
    }
    consider(seg);
    return out;
}

}  // namespace icmg::cli

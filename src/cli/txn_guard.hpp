// v2.26.0: transactional run (lite) -- `icmg run --txn`.
//
// The agent-infra market converged on diff/commit/abort changesets per agent
// run (DeltaBox Lite, Tilde.run, AWS Strands Box, k8s agent-sandbox; Oct
// 2026). Lite version: a git-tree snapshot of the working tree is taken
// before the command runs; afterwards the change set is reviewable
// (`--txn-diff`), acceptable (`--txn-commit`) or revertable (`--txn-abort`).
// HONEST layer: tracked files only, no filesystem isolation -- files created
// by the command and never git-added are listed but not auto-deleted.
#pragma once
#include <cstdint>
#include <string>

namespace icmg::cli {

struct TxnState {
    std::string tree;     // git tree hash of the pre-command working tree
    std::int64_t ts = 0;  // begin time (unix seconds)
};

inline std::string serializeTxnState(const TxnState& s) {
    return "{\"tree\":\"" + s.tree + "\",\"ts\":" + std::to_string(s.ts) + "}";
}

// Tolerant parse; garbage input yields an empty-tree state.
inline TxnState parseTxnState(const std::string& text) {
    TxnState s;
    auto tp = text.find("\"tree\":\"");
    if (tp == std::string::npos) return s;
    tp += 8;
    auto te = text.find('"', tp);
    if (te == std::string::npos) return s;
    s.tree = text.substr(tp, te - tp);
    auto sp = text.find("\"ts\":");
    if (sp != std::string::npos) {
        try { s.ts = std::stoll(text.substr(sp + 5)); } catch (...) {}
    }
    return s;
}

inline std::string txnStatePathFor(const std::string& root) {
    std::string r = root;
    if (!r.empty() && r.back() != '/' && r.back() != '\\') r += '/';
    return r + ".icmg/txn-state.json";
}

} // namespace icmg::cli

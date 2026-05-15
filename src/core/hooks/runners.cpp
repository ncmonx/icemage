#include "runners.hpp"
#include "internals.hpp"
#include "../config.hpp"
#include "../db.hpp"
#include "../exec_utils.hpp"
#include "../path_utils.hpp"

#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::core::hooks {

// v0.57.0: runners no longer fork distill/compliance/budget/compress
// subprocesses — they call the in-process lib functions in internals.cpp.
// Net: ~200-400ms saved per Stop event (4 forks × ~50-100ms each).

// ---- Stop ------------------------------------------------------------------

// v1.1.0 Task 7: async fast-path.
// Synchronous part runs only the trivial in-process tasks (tool-budget reset);
// distill / fail-sync / compliance run on a detached worker so the hook
// returns to Claude Code within ~ms instead of ~150-300ms.
//
// Tradeoff: if the agent process exits within ~1s of Stop event firing, the
// async work may not finish. memory_nodes writes are best-effort already; on
// next SessionStart icmg picks up where it left off.
std::string runStopHook(const std::string& stdin_raw) {
    if (std::getenv("ICMG_NO_STOP_HOOK")) return "";
    if (stdin_raw.empty()) return "";

    // Sync: cheap + immediate-feedback work only.
    toolBudgetReset();

    // Async: heavyweight work that the agent doesn't need synchronously.
    // Detach so the hook handler can return immediately. std::thread dtor
    // would otherwise std::terminate on a non-joined thread.
    if (!std::getenv("ICMG_STOP_SYNC")) {
        std::thread([raw = stdin_raw]() {
            try {
                (void)distillAuto(raw, /*min_len=*/100);
                (void)failSyncDenials();
                (void)complianceCheckThinking(raw, /*max_words=*/80);
            } catch (...) {}
        }).detach();
        return "";
    }

    // ICMG_STOP_SYNC=1 retains legacy synchronous path for debug / tests.
    (void)distillAuto(stdin_raw, /*min_len=*/100);
    (void)failSyncDenials();
    (void)complianceCheckThinking(stdin_raw, /*max_words=*/80);
    return "";
}

// ---- PreCompact ------------------------------------------------------------

std::string runPreCompactHook(const std::string& stdin_raw) {
    if (std::getenv("ICMG_NO_PRECOMPACT_HOOK")) return "";

    // Step 1: snapshot via existing Python script (kept as subprocess —
    // sidecar work, lib extraction out of v0.57.0 scope).
    fs::path snap = fs::current_path() / ".claude" / "hooks" / "icmg-precompact-snapshot.py";
    if (fs::exists(snap)) {
        std::string cmd = "python3 \"" + snap.string()
                        + "\" 2>/dev/null || python \"" + snap.string()
                        + "\" 2>/dev/null || true";
        (void)safeExecShell(cmd, false, 30000);
    }

    // Step 2: distill session transcript (in-process now).
    if (!stdin_raw.empty()) {
        try {
            json j = json::parse(stdin_raw);
            std::string transcript = j.value("transcript", std::string(""));
            if (!transcript.empty()) {
                (void)distillSession(transcript);
            }
        } catch (...) {}
    }

    // Step 3: ABSOLUTE RULE + top-5 pinned anchors.
    std::ostringstream rule;
    rule << "ABSOLUTE RULE - icmg FIRST. Before any native Read/Bash/Grep/Glob/"
            "WebFetch, check icmg equivalent.\n";
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        rule << "Pinned decisions:\n";
        int n = 0;
        db.query(
            "SELECT topic, stance FROM decisions WHERE pinned = 1 "
            "AND superseded_at IS NULL ORDER BY made_at DESC LIMIT 5",
            {}, [&](const Row& r){
                if (r.size() < 2) return;
                rule << "  - " << r[0] << ": " << r[1] << "\n";
                ++n;
            });
        if (n == 0) rule << "  (none pinned)\n";
    } catch (...) {}

    json out;
    out["hookSpecificOutput"]["hookEventName"] = "PreCompact";
    out["hookSpecificOutput"]["additionalContext"] = rule.str();
    return out.dump();
}

// ---- PostToolUse:Read ------------------------------------------------------

std::string runPostToolUseReadHook(const std::string& stdin_raw) {
    if (std::getenv("ICMG_NO_COMPRESS_HOOK")) return "";
    if (stdin_raw.empty()) return "";

    std::string content;
    try {
        json j = json::parse(stdin_raw);
        if (j.contains("tool_response")) {
            auto& tr = j["tool_response"];
            if (tr.contains("content") && tr["content"].is_string())
                content = tr["content"].get<std::string>();
            else if (tr.contains("output") && tr["output"].is_string())
                content = tr["output"].get<std::string>();
        }
    } catch (...) { return ""; }

    if (content.size() < 1024) return "";

    std::string compressed = compressInPlace(content, /*threshold=*/256);
    if (compressed.empty()) return "";

    std::ostringstream msg;
    msg << "Read output auto-compressed (" << content.size() << "B -> "
        << compressed.size() << "B). Glossary inline; aliases match original tokens.\n"
        << compressed;

    json out;
    out["hookSpecificOutput"]["hookEventName"] = "PostToolUse";
    out["hookSpecificOutput"]["additionalContext"] = msg.str();
    return out.dump();
}

} // namespace icmg::core::hooks

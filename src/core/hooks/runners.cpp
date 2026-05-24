#include "runners.hpp"
#include "internals.hpp"
#include "../config.hpp"
#include "../db.hpp"
#include "../exec_utils.hpp"
#include "../path_utils.hpp"
// v1.32.0 C1: optional LLM PreCompact summarize.
#include "../../llm/warm_pool.hpp"
#include "../../llm/smart_router.hpp"
#include "../../llm/llama_runner.hpp"

#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

    // v1.6.4: process pending graph-scan queue (deferred from PostToolUse:Edit).
    // Targeted per-file scan via `--file <path>` flag — fast (<500ms each),
    // skips O(N) mem-sync post-pass. Single subprocess for all pending files.
    // Detached + 60s budget cap so Stop hook returns immediately and runaway
    // scans (e.g. corrupted file) cannot eat resources indefinitely.
    try {
        fs::path pending = fs::path(icmgGlobalDir()) / "pending-graph-scan.list";
        if (fs::exists(pending)) {
            std::vector<std::string> files;
            {
                std::ifstream f(pending);
                std::string line;
                while (std::getline(f, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) files.push_back(line);
                }
            }
            std::error_code ec;
            fs::remove(pending, ec);  // truncate to avoid double-processing
            if (!files.empty()) {
                std::string cmd = "icmg graph scan";
                for (auto& f : files) {
                    // Shell-escape: wrap in double quotes; backslashes are safe
                    // on Windows; on POSIX no spaces in source-tree paths is
                    // typical, but quoting handles edge cases either way.
                    cmd += " --file \"" + f + "\"";
                }
                cmd += " >> .icmg/scan.log 2>&1";
                // Detached: Stop hook returns ms. 60s budget cap on wall time —
                // even cold scans of large repos finish well under this; runaway
                // (corrupted file, parser hang) gets killed.
                (void)safeExecShell(cmd, /*detach=*/true, /*timeout_ms=*/60000);
            }
        }
    } catch (...) {}

    // Async: heavyweight work that the agent doesn't need synchronously.
    // Detach so the hook handler can return immediately. std::thread dtor
    // would otherwise std::terminate on a non-joined thread.
    if (!std::getenv("ICMG_STOP_SYNC")) {
        std::thread([raw = stdin_raw]() {
            try {
                (void)distillAuto(raw, /*min_len=*/100);
                (void)failSyncDenials();
                (void)complianceCheckThinking(raw, /*max_words=*/80);
                // v1.38.0 A7: amnesia counter — scan AI response for prior
                // decision match. Logs to amnesia_events for next prompt.
                (void)runStopAmnesiaScan(raw);
            } catch (...) {}
        }).detach();
        return "";
    }

    // ICMG_STOP_SYNC=1 retains legacy synchronous path for debug / tests.
    (void)distillAuto(stdin_raw, /*min_len=*/100);
    (void)failSyncDenials();
    (void)complianceCheckThinking(stdin_raw, /*max_words=*/80);
    (void)runStopAmnesiaScan(stdin_raw); // v1.38.0 A7
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
    // v1.21.4 (X1): per-snippet durable knowledge — standalone BM25-hittable
    //   nodes that survive compaction.
    // v1.21.7 (FB2): persist raw transcript into FTS5-indexed `transcripts`
    //   so users can later `icmg transcript search` over historical chats.
    if (!stdin_raw.empty()) {
        try {
            json j = json::parse(stdin_raw);
            std::string transcript = j.value("transcript", std::string(""));
            std::string session_id = j.value("session_id", std::string(""));
            if (!transcript.empty()) {
                (void)recordTranscript(session_id, transcript);   // FB2
                (void)extractPreCompactSnippets(transcript);      // X1
                (void)distillSession(transcript);
            }
        } catch (...) {}
    }

    // v1.32.0 C1: LLM PreCompact summarize (cold path, <15 s SLA).
    // Router-gated. If LLM_LOCAL, summarize the transcript via warm-pool
    // Qwen 0.5B and append the summary to additionalContext. Falls back
    // silently to existing regex distill (step 2 above already ran).
    std::string llm_summary;
    if (!stdin_raw.empty() && !std::getenv("ICMG_NO_LLM_PRECOMPACT")) {
        try {
            json j = json::parse(stdin_raw);
            std::string transcript = j.value("transcript", std::string(""));
            if (!transcript.empty()) {
                constexpr std::size_t kMax = 8 * 1024;
                if (transcript.size() > kMax)
                    transcript = transcript.substr(transcript.size() - kMax);
                icmg::llm::CallContext rctx;
                rctx.tier             = icmg::llm::PathTier::COLD;
                rctx.kind             = "precompact";
                rctx.input_tokens_est = transcript.size() / 4;
                rctx.build_has_llama  = icmg::llm::LlamaRunner::available();
                rctx.llm_loaded       = icmg::llm::WarmPool::instance().isLoaded();
                const char* dis = std::getenv("ICMG_LLM_USER_DISABLED");
                rctx.user_disabled = (dis && *dis == '1');
                auto rd = icmg::llm::routeFor(rctx);
                if (rd.route == icmg::llm::Route::LLM_LOCAL) {
                    std::string err;
                    auto* run = icmg::llm::WarmPool::instance().acquire(err);
                    if (run) {
                        std::string prompt =
                            "You compress AI coding session transcripts before context "
                            "compaction. Extract <=6 durable facts as bullets "
                            "`- <fact>` covering: open tasks, decisions made, anti-"
                            "patterns avoided, blockers. Drop chit-chat. No prose.\n\n"
                            "Transcript:\n" + transcript + "\n\nFacts:\n";
                        icmg::llm::InferParams ip;
                        ip.max_tokens = 256;
                        ip.temperature = 0.2f;
                        auto res = run->infer(prompt, ip);
                        if (res.ok && !res.text.empty()) llm_summary = res.text;
                    }
                }
            }
        } catch (...) {}
    }

    // Step 3: ABSOLUTE RULE + top-5 pinned anchors (+ optional LLM summary).
    std::ostringstream rule;
    rule << "ABSOLUTE RULE - icmg FIRST. Before any native Read/Bash/Grep/Glob/"
            "WebFetch, check icmg equivalent.\n";
    if (!llm_summary.empty()) {
        rule << "LLM-distilled session facts (Qwen 0.5B, pre-compact):\n"
             << llm_summary << "\n";
    }
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

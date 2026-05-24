// v1.31.0 B3a: in-process LLM warm-pool.
//
// Singleton LlamaRunner that lazy-loads the active model on first
// request, caches for process lifetime, reuses across multiple infer
// calls within the same invocation.
//
// Used by:
//   - `icmg pack --rerank` (B4)
//   - `icmg compact-bg` detached worker (A9)
//   - icmg-service tick handlers (subset, opt-in)
//
// NOT used by:
//   - hot-path hooks (PreToolUse:*, UserPromptSubmit) — CI-lint enforced
//
// Cross-process warm-pool (icmg-service holding model + IPC infer
// endpoint) deferred to v1.32 as B3b. The in-process variant covers
// single-invocation tools that issue multiple LLM calls without
// paying repeated cold-load cost.
//
// Thread-safety: load is mutex-guarded; infer is mutex-guarded by
// LlamaRunner itself (single-threaded by design). Concurrent infer
// requests serialize — acceptable because warm/cold cadence is 1-10/min.
#pragma once
#include "llama_runner.hpp"
#include <memory>
#include <mutex>
#include <string>

namespace icmg::llm {

class WarmPool {
public:
    static WarmPool& instance();

    // Returns a loaded runner ready to infer(). Lazy-loads from the
    // active model resolved via ~/.icmg/llm/active on first call.
    // Returns nullptr + populates `err` on any failure path (build off,
    // opt-out, no active, missing file, load fail, RAM guard).
    // Callers must hold the returned pointer only for the duration of
    // their infer() call — singleton owns lifetime.
    LlamaRunner* acquire(std::string& err);

    // Cheap status — does NOT trigger load.
    bool isLoaded() const;
    std::string activeModelId() const;

    // Drop the loaded model (free RAM). Next acquire() triggers a fresh
    // cold-load. Used by `icmg llm bench --reset` and shutdown paths.
    void release();

private:
    WarmPool() = default;
    mutable std::mutex            mu_;
    std::unique_ptr<LlamaRunner>  runner_;
    std::string                   active_id_;
};

} // namespace icmg::llm

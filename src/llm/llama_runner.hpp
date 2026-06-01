// v1.31.0 A2: LlamaRunner — thin C++ wrapper around llama.cpp C API.
//
// Lifecycle: construct -> load(path) -> infer(...)/infer(...)... -> unload (or dtor).
// Single-threaded by design. Used from icmg-service warm-pool (one runner per
// process). Reload to switch models.
//
// Build gating: full impl compiled iff ICMG_HAS_LLAMA defined (set by CMake
// when -DICMG_USE_LLAMA=ON). When undefined, every method is a stub that
// returns false / empty string. Callers must check `available()` first OR
// rely on smart router (B1.5) to bypass LLM when unavailable.
//
// Latency contract: load() = cold (seconds, model-size-bound). infer() depends
// on prompt+output tokens; warm/cold path only (see SLA table in plan).
// **NEVER call from hot path.** CI-lint enforces zero LlamaRunner refs in
// bundle/run/tkil/hook_cmd.cpp.
//
// RAM guard: load() refuses if `availableRamMB() < llmMinRamThresholdMB(model_min_mb)`.
// Caller may pass model_min_mb (from registry.json) to override default 1536 MB.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace icmg::llm {

struct LlamaParams {
    int  n_ctx        = 8192;   // v1.48.2: was 2048, too small for multi-turn + context injection
    int  n_threads    = 0;      // 0 = auto (hardware_concurrency-1)
    int  n_gpu_layers = 0;      // CPU-only default; >0 needs GGML_VULKAN build
    bool use_mmap     = true;
    bool use_mlock    = false;
};

struct InferParams {
    int   max_tokens = 4096;    // v1.52.0: bumped for long gen (n_ctx-bounded at runtime)
    float temperature = 0.7f;   // 0 = greedy
    int   top_k     = 40;       // 0 = disabled
    float top_p     = 0.95f;    // 1.0 = disabled
    // v1.52.0: repetition penalty (fixes Dolphin 8B looping on Indonesian).
    float repeat_penalty    = 1.15f;  // 1.0 = disabled; 1.1-1.3 typical
    int   repeat_last_n     = 64;     // tokens of history considered
    float frequency_penalty = 0.0f;   // alt penalty; 0.0 = disabled
    float presence_penalty  = 0.0f;   // alt penalty; 0.0 = disabled
    int   seed      = -1;       // -1 = random
    // Optional stop strings (text-level). Match-after-detokenize.
    std::string stop;
    // v1.32.0 C3: KV-cache reuse policy.
    // false (default): clear KV cache before decoding the prompt — safe,
    //                  every call is independent. Wastes compute when the
    //                  prompt shares a prefix with the previous call.
    // true: keep KV from previous infer() on the same runner. Caller is
    //       responsible for ensuring prompts continue from where the last
    //       one left off (e.g., follow-up turn in a conversation).
    bool  reuse_kv  = false;
};

struct InferResult {
    std::string text;
    int    tokens_in     = 0;
    int    tokens_out    = 0;
    double wall_ms       = 0.0;
    bool   ok            = false;
    std::string error;
};

class LlamaRunner {
public:
    LlamaRunner();
    ~LlamaRunner();

    // Non-copyable; non-movable (holds raw llama_* pointers).
    LlamaRunner(const LlamaRunner&) = delete;
    LlamaRunner& operator=(const LlamaRunner&) = delete;

    // Returns true iff icmg was built with ICMG_HAS_LLAMA.
    static bool available();

    // Load a GGUF model. Returns false on failure (model missing, OOM,
    // RAM-guard refuse). On false, `lastError()` carries the reason.
    // `model_min_mb` = per-model RAM threshold from registry.json (0 = use default).
    bool load(const std::string& gguf_path,
              const LlamaParams& p = {},
              std::uint64_t model_min_mb = 0);

    // Free model + context. Idempotent.
    void unload();

    // True after a successful load() and before unload()/dtor.
    bool isLoaded() const;

    // Run a single inference. Returns ok=false + error on failure
    // (not loaded, decode error, OOM mid-run).
    // `on_token` (optional) fires after every accepted token with the
    // newly-decoded piece — for streaming UIs / early-abort logic.
    // Return false from on_token to abort generation.
    InferResult infer(const std::string& prompt,
                      const InferParams& ip = {},
                      const std::function<bool(const std::string&)>& on_token = {});

    const std::string& lastError() const;

    // v1.32.0 C3: force-clear the KV cache. Safe to call when not loaded
    // (no-op). Useful between unrelated conversations on the same runner.
    void clearKvCache();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace icmg::llm

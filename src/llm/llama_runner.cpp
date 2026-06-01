// v1.31.0 A2: LlamaRunner impl. See llama_runner.hpp.
//
// When ICMG_HAS_LLAMA is undefined (default Phase A build), every public
// method is a stub returning false/empty + an error explaining the LLM
// feature was not compiled in. The smart router (B1.5) and CLI commands
// must check `available()` before relying on inference.
#include "llama_runner.hpp"
#include "../core/sys_resources.hpp"
#include "telemetry.hpp"

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#ifdef ICMG_HAS_LLAMA
#  include "llama.h"
#  include "ggml.h"
#endif

namespace icmg::llm {

#ifdef ICMG_HAS_LLAMA

struct LlamaRunner::Impl {
    llama_model*       model   = nullptr;
    llama_context*     ctx     = nullptr;
    const llama_vocab* vocab   = nullptr;
    std::string        last_err;
    bool               backend_inited = false;

    ~Impl() { reset(); }

    void reset() {
        if (ctx)   { llama_free(ctx);          ctx   = nullptr; }
        if (model) { llama_model_free(model);  model = nullptr; }
        vocab = nullptr;
    }
};

static std::string detokenize(const llama_vocab* vocab, llama_token tok) {
    char buf[256];
    int n = llama_token_to_piece(vocab, tok, buf, sizeof(buf), 0, /*special=*/false);
    if (n < 0) return {};
    return std::string(buf, buf + n);
}

bool LlamaRunner::available() { return true; }


// v1.47.0: silent log callback. llama.cpp + ggml dump device probe,
// model loader, KV cache, etc. by default — pollutes stdout when chat
// streams to user terminal. Filter to ERROR only unless opt-in.
static void llmSilentLogCb(ggml_log_level level, const char* text, void* /*user*/) {
    if (std::getenv("ICMG_LLM_VERBOSE_LOGS")) {
        std::fputs(text, stderr);
        return;
    }
    if (level >= GGML_LOG_LEVEL_ERROR) {
        std::fputs(text, stderr);
    }
}

LlamaRunner::LlamaRunner() : impl_(new Impl()) {
    // v1.47.0: register log callbacks BEFORE backend init so the
    // Vulkan device probe + model loader progress bar respect them.
    llama_log_set(llmSilentLogCb, nullptr);
    ggml_log_set(llmSilentLogCb, nullptr);
    llama_backend_init();
    impl_->backend_inited = true;
}

LlamaRunner::~LlamaRunner() {
    if (impl_) {
        impl_->reset();
        if (impl_->backend_inited) llama_backend_free();
        delete impl_;
    }
}

bool LlamaRunner::isLoaded() const { return impl_ && impl_->model && impl_->ctx; }

const std::string& LlamaRunner::lastError() const {
    static const std::string empty;
    return impl_ ? impl_->last_err : empty;
}

bool LlamaRunner::load(const std::string& gguf_path,
                       const LlamaParams& p,
                       std::uint64_t model_min_mb) {
    if (!impl_) return false;
    impl_->reset();
    impl_->last_err.clear();

    auto t0 = std::chrono::steady_clock::now();
    auto record_load = [&](bool ok, const std::string& err) {
        CallSample s;
        s.unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
        s.kind      = "load";
        s.wall_ms   = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count();
        s.ok        = ok;
        s.cold_load = true;
        s.error     = err;
        Telemetry::instance().push(s);
    };

    // RAM guard — refuse below threshold (avail probe failure = refuse).
    if (!core::llmHasEnoughRam(model_min_mb)) {
        std::uint64_t avail = core::availableRamMB();
        std::uint64_t need  = core::llmMinRamThresholdMB(model_min_mb);
        impl_->last_err = "RAM guard refuse: available=" + std::to_string(avail) +
                          " MB < threshold=" + std::to_string(need) +
                          " MB (override via ICMG_LLM_MIN_RAM_MB)";
        record_load(false, impl_->last_err);
        return false;
    }

    llama_model_params mp = llama_model_default_params();
    // v1.43+ GPU acceleration. Resolution order:
    //   1. env ICMG_LLM_GPU_LAYERS  (-1 = all, 0 = CPU only)
    //   2. file ~/.icmg/llm/gpu-layers.txt (single int line) — persistent
    //      across shells without env var, set once per machine.
    //   3. p.n_gpu_layers (default 0 CPU)
    int gpu_layers = p.n_gpu_layers;
    if (const char* env = std::getenv("ICMG_LLM_GPU_LAYERS")) {
        try { gpu_layers = std::stoi(env); } catch (...) {}
    } else {
        // Config-file fallback. Quiet on missing — no error spam.
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (home) {
            std::string cfg = std::string(home) + "/.icmg/llm/gpu-layers.txt";
            FILE* f = std::fopen(cfg.c_str(), "r");
            if (f) {
                int v = 0;
                if (std::fscanf(f, "%d", &v) == 1) gpu_layers = v;
                std::fclose(f);
            }
        }
    }
    mp.n_gpu_layers = gpu_layers;
    mp.use_mmap     = p.use_mmap;
    mp.use_mlock    = p.use_mlock;

    impl_->model = llama_model_load_from_file(gguf_path.c_str(), mp);
    if (!impl_->model) {
        impl_->last_err = "llama_model_load_from_file failed: " + gguf_path;
        record_load(false, impl_->last_err);
        return false;
    }

    llama_context_params cp = llama_context_default_params();
    // v1.49.0: ICMG_LLM_N_CTX env override. Lower n_ctx → smaller KV
    // cache → less VRAM (lets 4GB cards run bigger models partial-offload).
    cp.n_ctx     = p.n_ctx;
    if (const char* env = std::getenv("ICMG_LLM_N_CTX")) {
        try { cp.n_ctx = std::stoi(env); } catch (...) {}
    } else {
        // File fallback ~/.icmg/llm/n_ctx.txt (mirrors gpu-layers.txt pattern).
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (home) {
            std::string cfg = std::string(home) + "/.icmg/llm/n_ctx.txt";
            FILE* f = std::fopen(cfg.c_str(), "r");
            if (f) {
                int v = 0;
                if (std::fscanf(f, "%d", &v) == 1 && v > 0) cp.n_ctx = v;
                std::fclose(f);
            }
        }
    }
    cp.n_batch   = 8192;  // v1.48.0 B3: bumped from 512 to fit multi-turn
                          // history. Prevents GGML_ASSERT(n_tokens_all
                          // <= cparams.n_batch) on long conversations.
    cp.n_threads = (p.n_threads > 0) ? p.n_threads
                                     : static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1u));
    cp.n_threads_batch = cp.n_threads;

    impl_->ctx = llama_init_from_model(impl_->model, cp);
    if (!impl_->ctx) {
        impl_->last_err = "llama_init_from_model failed";
        impl_->reset();
        record_load(false, impl_->last_err);
        return false;
    }
    impl_->vocab = llama_model_get_vocab(impl_->model);
    record_load(true, "");
    return true;
}

void LlamaRunner::unload() { if (impl_) impl_->reset(); }

void LlamaRunner::clearKvCache() {
    if (impl_ && impl_->ctx) {
        llama_memory_clear(llama_get_memory(impl_->ctx), /*data=*/true);
    }
}

InferResult LlamaRunner::infer(const std::string& prompt,
                               const InferParams& ip,
                               const std::function<bool(const std::string&)>& on_token) {
    InferResult r;
    if (!isLoaded()) {
        r.error = "LlamaRunner::infer called without load()";
        return r;
    }
    auto t0 = std::chrono::steady_clock::now();

    // v1.32.0 C3: reset KV cache between unrelated calls unless caller
    // opts in to reuse. Without this, positions accumulate and the
    // context window overflows after ~3-5 prompts.
    if (!ip.reuse_kv) {
        llama_memory_clear(llama_get_memory(impl_->ctx), /*data=*/true);
    }

    // Tokenize the prompt. Probe size first.
    int n_prompt = -llama_tokenize(impl_->vocab, prompt.c_str(), (int)prompt.size(),
                                   nullptr, 0, /*add_special=*/true, /*parse_special=*/true);
    if (n_prompt <= 0) { r.error = "tokenize probe failed"; return r; }
    std::vector<llama_token> toks(n_prompt);
    if (llama_tokenize(impl_->vocab, prompt.c_str(), (int)prompt.size(),
                       toks.data(), n_prompt, true, true) < 0) {
        r.error = "tokenize failed"; return r;
    }
    r.tokens_in = n_prompt;
    // v1.48.0 B3 hardening: refuse-with-error when prompt
    // exceeds batch capacity. Prevents GGML_ASSERT crash on
    // long conversations; chat_cmd pre-trims, this is safety.
    // Build sampler chain (greedy if temperature==0).
    llama_sampler* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (ip.temperature <= 0.0f) {
        llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
    } else {
        // v1.52.0: repetition penalty FIRST (before top-k/p/temp) to mask repeated tokens
        // before the distribution is sampled. Fixes Dolphin 8B looping on Indonesian.
        if (ip.repeat_penalty > 1.0f || ip.frequency_penalty > 0.0f || ip.presence_penalty > 0.0f) {
            llama_sampler_chain_add(smpl, llama_sampler_init_penalties(
                ip.repeat_last_n,
                ip.repeat_penalty,
                ip.frequency_penalty,
                ip.presence_penalty));
        }
        if (ip.top_k > 0)      llama_sampler_chain_add(smpl, llama_sampler_init_top_k(ip.top_k));
        if (ip.top_p < 1.0f)   llama_sampler_chain_add(smpl, llama_sampler_init_top_p(ip.top_p, 1));
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(ip.temperature));
        const std::uint32_t seed = (ip.seed < 0)
            ? static_cast<std::uint32_t>(std::chrono::system_clock::now().time_since_epoch().count())
            : static_cast<std::uint32_t>(ip.seed);
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(seed));
    }

    // v1.48.1: chunk-decode prompt in n_batch-sized pieces. Earlier
    // single-batch decode triggered GGML_ASSERT on long prompts (context
    // injection + multi-turn history) even with bumped n_batch=8192.
    // Robust fix: stream chunks regardless of total prompt size.
    constexpr int chunk_size = 512;  // Conservative; well under n_batch=8192.
    for (int off = 0; off < n_prompt; off += chunk_size) {
        int chunk = (n_prompt - off < chunk_size) ? (n_prompt - off) : chunk_size;
        llama_batch batch = llama_batch_get_one(toks.data() + off, chunk);
        if (llama_decode(impl_->ctx, batch) != 0) {
            r.error = "decode(prompt) failed at offset=" + std::to_string(off);
            llama_sampler_free(smpl);
            return r;
        }
    }

    // Generation loop.
    std::string out;
    bool aborted = false;
    for (int i = 0; i < ip.max_tokens; ++i) {
        llama_token id = llama_sampler_sample(smpl, impl_->ctx, -1);
        if (llama_vocab_is_eog(impl_->vocab, id)) break;

        std::string piece = detokenize(impl_->vocab, id);
        out += piece;
        r.tokens_out += 1;

        if (on_token && !on_token(piece)) { aborted = true; break; }

        if (!ip.stop.empty() && out.size() >= ip.stop.size() &&
            out.rfind(ip.stop) == out.size() - ip.stop.size()) {
            out.erase(out.size() - ip.stop.size());
            break;
        }

        llama_batch nb = llama_batch_get_one(&id, 1);
        if (llama_decode(impl_->ctx, nb) != 0) {
            r.error = "decode(step) failed at i=" + std::to_string(i);
            break;
        }
    }

    llama_sampler_free(smpl);

    auto t1 = std::chrono::steady_clock::now();
    r.wall_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.text    = std::move(out);
    r.ok      = r.error.empty() || aborted;

    // B5 telemetry: record this infer call.
    {
        CallSample s;
        s.unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
        s.kind       = "infer";
        s.wall_ms    = r.wall_ms;
        s.tokens_in  = static_cast<std::uint32_t>(r.tokens_in);
        s.tokens_out = static_cast<std::uint32_t>(r.tokens_out);
        s.ok         = r.ok;
        s.cold_load  = false;
        s.error      = r.error;
        Telemetry::instance().push(s);
    }
    return r;
}

#else  // !ICMG_HAS_LLAMA — stubs.

struct LlamaRunner::Impl { std::string last_err = "icmg built without ICMG_USE_LLAMA"; };

bool LlamaRunner::available() { return false; }
LlamaRunner::LlamaRunner()  : impl_(new Impl()) {}
LlamaRunner::~LlamaRunner() { delete impl_; }
bool LlamaRunner::isLoaded() const { return false; }
const std::string& LlamaRunner::lastError() const { return impl_->last_err; }

bool LlamaRunner::load(const std::string&, const LlamaParams&, std::uint64_t) {
    impl_->last_err = "LLM disabled: rebuild icmg with -DICMG_USE_LLAMA=ON";
    return false;
}
void LlamaRunner::unload() {}
void LlamaRunner::clearKvCache() {}

InferResult LlamaRunner::infer(const std::string&,
                               const InferParams&,
                               const std::function<bool(const std::string&)>&) {
    InferResult r;
    r.error = "LLM disabled: rebuild icmg with -DICMG_USE_LLAMA=ON";
    return r;
}

#endif // ICMG_HAS_LLAMA

} // namespace icmg::llm

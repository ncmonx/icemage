// v1.31.0 A2: LlamaRunner impl. See llama_runner.hpp.
//
// When ICMG_HAS_LLAMA is undefined (default Phase A build), every public
// method is a stub returning false/empty + an error explaining the LLM
// feature was not compiled in. The smart router (B1.5) and CLI commands
// must check `available()` before relying on inference.
#include "llama_runner.hpp"
#include "../core/sys_resources.hpp"

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#ifdef ICMG_HAS_LLAMA
#  include "llama.h"
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

LlamaRunner::LlamaRunner() : impl_(new Impl()) {
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

    // RAM guard — refuse below threshold (avail probe failure = refuse).
    if (!core::llmHasEnoughRam(model_min_mb)) {
        std::uint64_t avail = core::availableRamMB();
        std::uint64_t need  = core::llmMinRamThresholdMB(model_min_mb);
        impl_->last_err = "RAM guard refuse: available=" + std::to_string(avail) +
                          " MB < threshold=" + std::to_string(need) +
                          " MB (override via ICMG_LLM_MIN_RAM_MB)";
        return false;
    }

    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = p.n_gpu_layers;
    mp.use_mmap     = p.use_mmap;
    mp.use_mlock    = p.use_mlock;

    impl_->model = llama_model_load_from_file(gguf_path.c_str(), mp);
    if (!impl_->model) {
        impl_->last_err = "llama_model_load_from_file failed: " + gguf_path;
        return false;
    }

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx     = p.n_ctx;
    cp.n_batch   = 512;
    cp.n_threads = (p.n_threads > 0) ? p.n_threads
                                     : static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1u));
    cp.n_threads_batch = cp.n_threads;

    impl_->ctx = llama_init_from_model(impl_->model, cp);
    if (!impl_->ctx) {
        impl_->last_err = "llama_init_from_model failed";
        impl_->reset();
        return false;
    }
    impl_->vocab = llama_model_get_vocab(impl_->model);
    return true;
}

void LlamaRunner::unload() { if (impl_) impl_->reset(); }

InferResult LlamaRunner::infer(const std::string& prompt,
                               const InferParams& ip,
                               const std::function<bool(const std::string&)>& on_token) {
    InferResult r;
    if (!isLoaded()) {
        r.error = "LlamaRunner::infer called without load()";
        return r;
    }
    auto t0 = std::chrono::steady_clock::now();

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

    // Build sampler chain (greedy if temperature==0).
    llama_sampler* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (ip.temperature <= 0.0f) {
        llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
    } else {
        if (ip.top_k > 0)      llama_sampler_chain_add(smpl, llama_sampler_init_top_k(ip.top_k));
        if (ip.top_p < 1.0f)   llama_sampler_chain_add(smpl, llama_sampler_init_top_p(ip.top_p, 1));
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(ip.temperature));
        const std::uint32_t seed = (ip.seed < 0)
            ? static_cast<std::uint32_t>(std::chrono::system_clock::now().time_since_epoch().count())
            : static_cast<std::uint32_t>(ip.seed);
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(seed));
    }

    // Decode prompt (one big batch).
    llama_batch batch = llama_batch_get_one(toks.data(), (int)toks.size());
    if (llama_decode(impl_->ctx, batch) != 0) {
        r.error = "decode(prompt) failed";
        llama_sampler_free(smpl);
        return r;
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

InferResult LlamaRunner::infer(const std::string&,
                               const InferParams&,
                               const std::function<bool(const std::string&)>&) {
    InferResult r;
    r.error = "LLM disabled: rebuild icmg with -DICMG_USE_LLAMA=ON";
    return r;
}

#endif // ICMG_HAS_LLAMA

} // namespace icmg::llm

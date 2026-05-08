// Phase 40: real ONNX Runtime embedder via C API.
//
// Compiled only when ICMG_HAS_ONNX is defined. Uses the ORT C API (not C++)
// because the C ABI is stable across MSVC and MinGW — onnxruntime.dll is
// MSVC-built but we link to it from MinGW gcc by going through the C ABI.
//
// Model expectations: sentence-transformers/all-MiniLM-L6-v2 ONNX export.
//   inputs: input_ids[1,seq] int64, attention_mask[1,seq] int64, token_type_ids[1,seq] int64
//   output: last_hidden_state[1,seq,384] float32
//
// Pipeline: WordPiece encode → ORT Run → mask-weighted mean pool → L2 normalize.
//
// Locates model + vocab via:
//   ICMG_ONNX_MODEL env  → path to .onnx
//   ICMG_ONNX_VOCAB env  → path to vocab.txt
//   default: ~/.icmg/embed/all-MiniLM-L6-v2.onnx + ~/.icmg/embed/vocab.txt

#ifdef ICMG_HAS_ONNX

#include "embedder.hpp"
#include "wordpiece_tokenizer.hpp"
#include "../core/logger.hpp"

#include <onnxruntime_c_api.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::embed {

namespace {

// Load API once.
const OrtApi* ortApi() {
    static const OrtApi* api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    return api;
}

#define ORT_CHECK(expr, ctx) \
    do { OrtStatus* _s = (expr); \
         if (_s) { \
             const char* msg = ortApi()->GetErrorMessage(_s); \
             core::Logger::instance().warn(std::string("[onnx] ") + ctx + ": " + msg); \
             ortApi()->ReleaseStatus(_s); \
             return {}; \
         } \
    } while (0)

class OnnxEmbedder : public Embedder {
public:
    OnnxEmbedder() {
        // Resolve paths.
        const char* env_model = std::getenv("ICMG_ONNX_MODEL");
        const char* env_vocab = std::getenv("ICMG_ONNX_VOCAB");
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");

        if (env_model && fs::exists(env_model)) model_path_ = env_model;
        else if (home) {
            fs::path p = fs::path(home) / ".icmg" / "embed" / "all-MiniLM-L6-v2.onnx";
            if (fs::exists(p)) model_path_ = p.string();
        }

        std::string vocab_path;
        if (env_vocab && fs::exists(env_vocab)) vocab_path = env_vocab;
        else if (home) {
            fs::path v = fs::path(home) / ".icmg" / "embed" / "vocab.txt";
            if (fs::exists(v)) vocab_path = v.string();
        }

        if (!model_path_.empty() && !vocab_path.empty()) {
            tokenizer_ready_ = tokenizer_.loadVocab(vocab_path);
            available_ = tokenizer_ready_;
        }
    }

    ~OnnxEmbedder() override { release(); }

    bool        available() const override { return available_; }
    int         dim() const override       { return 384; }
    std::string model() const override     { return "all-MiniLM-L6-v2 (onnx)"; }

    std::vector<float> embed(const std::string& text) override {
        if (!available_) return {};
        std::lock_guard<std::mutex> lk(mu_);
        if (!ensureSession()) return {};

        // Tokenize.
        auto tk = tokenizer_.encode(text, 128);
        const int64_t seq = (int64_t)tk.input_ids.size();
        if (seq == 0) return {};

        std::vector<int64_t> token_type(seq, 0);
        const int64_t shape[2] = {1, seq};

        // Memory info.
        OrtMemoryInfo* mem = nullptr;
        ORT_CHECK(ortApi()->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem),
                  "CreateCpuMemoryInfo");

        // Build input tensors.
        OrtValue* in_ids = nullptr;
        OrtValue* in_mask = nullptr;
        OrtValue* in_typ = nullptr;
        ORT_CHECK(ortApi()->CreateTensorWithDataAsOrtValue(
            mem, tk.input_ids.data(), seq * sizeof(int64_t), shape, 2,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &in_ids), "create input_ids");
        ORT_CHECK(ortApi()->CreateTensorWithDataAsOrtValue(
            mem, tk.attention_mask.data(), seq * sizeof(int64_t), shape, 2,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &in_mask), "create attention_mask");
        ORT_CHECK(ortApi()->CreateTensorWithDataAsOrtValue(
            mem, token_type.data(), seq * sizeof(int64_t), shape, 2,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &in_typ), "create token_type_ids");

        const char* in_names[]  = {"input_ids", "attention_mask", "token_type_ids"};
        const OrtValue* in_vals[] = {in_ids, in_mask, in_typ};
        const char* out_names[] = {"last_hidden_state"};
        OrtValue* out = nullptr;

        OrtStatus* run_status = ortApi()->Run(session_, /*RunOptions*/ nullptr,
                                              in_names, in_vals, 3,
                                              out_names, 1, &out);
        ortApi()->ReleaseMemoryInfo(mem);
        ortApi()->ReleaseValue(in_ids);
        ortApi()->ReleaseValue(in_mask);
        ortApi()->ReleaseValue(in_typ);
        if (run_status) {
            const char* msg = ortApi()->GetErrorMessage(run_status);
            core::Logger::instance().warn(std::string("[onnx] Run: ") + msg);
            ortApi()->ReleaseStatus(run_status);
            return {};
        }

        // Extract output [1, seq, 384].
        float* out_data = nullptr;
        ORT_CHECK(ortApi()->GetTensorMutableData(out, (void**)&out_data), "GetTensorMutableData");

        // Mask-weighted mean pool.
        const int D = 384;
        std::vector<float> pooled(D, 0.0f);
        float mask_sum = 0.0f;
        for (int64_t t = 0; t < seq; ++t) {
            float m = (float)tk.attention_mask[t];
            mask_sum += m;
            for (int d = 0; d < D; ++d) {
                pooled[d] += m * out_data[t * D + d];
            }
        }
        ortApi()->ReleaseValue(out);
        if (mask_sum < 1e-6f) return {};
        for (int d = 0; d < D; ++d) pooled[d] /= mask_sum;

        // L2 normalize.
        float norm = 0.0f;
        for (float v : pooled) norm += v * v;
        norm = std::sqrt(norm);
        if (norm < 1e-12f) return {};
        for (float& v : pooled) v /= norm;
        return pooled;
    }

private:
    bool ensureSession() {
        if (session_) return true;
        OrtEnv* env = nullptr;
        ORT_CHECK(ortApi()->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "icmg", &env), "CreateEnv");
        env_ = env;

        OrtSessionOptions* opts = nullptr;
        ORT_CHECK(ortApi()->CreateSessionOptions(&opts), "CreateSessionOptions");
        ortApi()->SetIntraOpNumThreads(opts, 1);
        ortApi()->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_BASIC);

#ifdef _WIN32
        // ORT expects wchar_t* path on Windows.
        std::wstring wpath(model_path_.begin(), model_path_.end());
        ORT_CHECK(ortApi()->CreateSession(env_, wpath.c_str(), opts, &session_), "CreateSession");
#else
        ORT_CHECK(ortApi()->CreateSession(env_, model_path_.c_str(), opts, &session_), "CreateSession");
#endif
        ortApi()->ReleaseSessionOptions(opts);
        return session_ != nullptr;
    }

    void release() {
        if (session_) { ortApi()->ReleaseSession(session_); session_ = nullptr; }
        if (env_)     { ortApi()->ReleaseEnv(env_);          env_ = nullptr; }
    }

    bool        available_ = false;
    bool        tokenizer_ready_ = false;
    std::string model_path_;
    WordPieceTokenizer tokenizer_;
    OrtEnv*     env_     = nullptr;
    OrtSession* session_ = nullptr;
    std::mutex  mu_;
};

} // anon

std::unique_ptr<Embedder> makeOnnxEmbedder() {
    auto e = std::make_unique<OnnxEmbedder>();
    if (!e->available()) return nullptr;
    return e;
}

} // namespace icmg::embed

#endif // ICMG_HAS_ONNX

// 2026-10-31: ONNX cross-encoder reranker -- see reranker.hpp.
//
// Model expectations: cross-encoder/ms-marco-MiniLM-L-6-v2 ONNX export
// (Xenova/ms-marco-MiniLM-L-6-v2 onnx/model.onnx).
//   inputs: input_ids / attention_mask / token_type_ids [1,seq] int64
//   output: logits [1,1] float32 (relevance, unbounded)
// One pair per Run (batch=1): no padding waste on short turns.
#include "reranker.hpp"

#ifdef ICMG_HAS_ONNX

#include "onnx_api.hpp"
#include "wordpiece_tokenizer.hpp"
#include "../core/logger.hpp"
#include "../core/path_u8.hpp"

#include <cstdlib>
#include <filesystem>
#include <mutex>

namespace fs = std::filesystem;

namespace icmg::embed {

namespace {

constexpr int kPairMaxTokens = 256;   // query + doc budget per forward pass
constexpr const char* kModelFile = "ms-marco-MiniLM-L-6-v2.onnx";
constexpr const char* kVocabFile = "ms-marco-vocab.txt";
constexpr const char* kVocabFallback = "vocab.txt";

// First existing path among env var / ~/.icmg/embed/<names...>, as UTF-8.
// getenv strings are in the ANSI code page -> fs::path(narrow) decodes them
// correctly; the result is normalised to UTF-8 so openSession can widen it.
std::string locate(const char* envVar, std::initializer_list<const char*> names) {
    if (const char* e = std::getenv(envVar); e && fs::exists(fs::path(e))) return core::pathU8(fs::path(e));
    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (!home) return "";
    for (const char* n : names) {
        fs::path p = fs::path(home) / ".icmg" / "embed" / n;
        if (fs::exists(p)) return core::pathU8(p);
    }
    return "";
}

void logStatus(OrtStatus* s, const char* ctx) {
    core::Logger::instance().warn(std::string("[rerank] ") + ctx + ": " +
                                  ortApi()->GetErrorMessage(s));
    ortApi()->ReleaseStatus(s);
}

class OnnxReranker : public Reranker {
public:
    bool init() {
        model_ = locate("ICMG_RERANK_MODEL", {kModelFile});
        std::string vocab = locate("ICMG_RERANK_VOCAB", {kVocabFile, kVocabFallback});
        if (model_.empty() || vocab.empty() || !ortApi()) return false;
        if (!tok_.loadVocab(vocab)) return false;
        return openSession();
    }

    ~OnnxReranker() override {
        if (session_) ortApi()->ReleaseSession(session_);
        if (env_) ortApi()->ReleaseEnv(env_);
    }

    std::string model() const override { return "ms-marco-MiniLM-L-6-v2 (onnx cross-encoder)"; }

    std::vector<float> score(const std::string& query,
                             const std::vector<std::string>& docs) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<float> out;
        out.reserve(docs.size());
        for (const auto& d : docs) {
            float s = 0.0f;
            if (!scoreOne(query, d, &s)) return {};
            out.push_back(s);
        }
        return out;
    }

private:
    bool openSession() {
        OrtStatus* st = ortApi()->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "icmg-rerank", &env_);
        if (st) { logStatus(st, "CreateEnv"); return false; }
        OrtSessionOptions* opts = nullptr;
        if ((st = ortApi()->CreateSessionOptions(&opts))) { logStatus(st, "SessionOptions"); return false; }
        ortApi()->SetIntraOpNumThreads(opts, 1);
        ortApi()->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_BASIC);
#ifdef _WIN32
        // UTF-8 -> UTF-16 via fs::path: keeps paths with spaces / non-ASCII intact.
        std::wstring w = core::u8ToPath(model_).wstring();
        st = ortApi()->CreateSession(env_, w.c_str(), opts, &session_);
#else
        st = ortApi()->CreateSession(env_, model_.c_str(), opts, &session_);
#endif
        ortApi()->ReleaseSessionOptions(opts);
        if (st) { logStatus(st, "CreateSession"); return false; }
        return true;
    }

    bool scoreOne(const std::string& q, const std::string& d, float* out) {
        auto p = tok_.encodePair(q, d, kPairMaxTokens);
        const int64_t shape[2] = {1, (int64_t)p.input_ids.size()};
        const size_t bytes = p.input_ids.size() * sizeof(int64_t);
        OrtMemoryInfo* mem = nullptr;
        OrtStatus* st = ortApi()->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem);
        if (st) { logStatus(st, "MemoryInfo"); return false; }
        OrtValue* in[3] = {nullptr, nullptr, nullptr};
        int64_t* bufs[3] = {p.input_ids.data(), p.attention_mask.data(), p.token_type_ids.data()};
        bool ok = true;
        for (int i = 0; i < 3 && ok; ++i) {
            st = ortApi()->CreateTensorWithDataAsOrtValue(mem, bufs[i], bytes, shape, 2,
                                                          ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &in[i]);
            if (st) { logStatus(st, "CreateTensor"); ok = false; }
        }
        OrtValue* res = nullptr;
        if (ok) {
            const char* inNames[] = {"input_ids", "attention_mask", "token_type_ids"};
            const char* outNames[] = {"logits"};
            st = ortApi()->Run(session_, nullptr, inNames, in, 3, outNames, 1, &res);
            if (st) { logStatus(st, "Run"); ok = false; }
        }
        if (ok) {
            float* data = nullptr;
            st = ortApi()->GetTensorMutableData(res, (void**)&data);
            if (st) { logStatus(st, "GetTensorData"); ok = false; }
            else *out = data[0];
        }
        if (res) ortApi()->ReleaseValue(res);
        for (auto* v : in) if (v) ortApi()->ReleaseValue(v);
        ortApi()->ReleaseMemoryInfo(mem);
        return ok;
    }

    std::string model_;
    WordPieceTokenizer tok_;
    OrtEnv* env_ = nullptr;
    OrtSession* session_ = nullptr;
    std::mutex mu_;
};

}  // namespace

std::unique_ptr<Reranker> makeReranker() {
    auto r = std::make_unique<OnnxReranker>();
    if (!r->init()) return nullptr;
    return r;
}

}  // namespace icmg::embed

#else  // !ICMG_HAS_ONNX

namespace icmg::embed {
std::unique_ptr<Reranker> makeReranker() { return nullptr; }
}  // namespace icmg::embed

#endif

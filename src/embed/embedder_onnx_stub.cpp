// Phase 33 scaffold: ONNX backend stub. Real impl in Phase 34.
//
// Compiled only when ICMG_HAS_ONNX is defined (set by CMake when ORT is found
// with -DICMG_USE_ONNX=ON). Currently a placeholder that reports availability
// but defers actual model loading.
//
// To implement Phase 34:
//   1. Load onnxruntime::Env + Session(model_path)
//   2. WordPiece tokenize input string -> input_ids + attention_mask
//   3. Run session, extract last_hidden_state
//   4. Mean-pool over tokens (mask-weighted) -> 384-dim vec
//   5. L2-normalize and return

#ifdef ICMG_HAS_ONNX

#include "embedder.hpp"
#include "../core/logger.hpp"
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace icmg::embed {

namespace {

class OnnxEmbedderStub : public Embedder {
public:
    OnnxEmbedderStub() {
        // Locate model: env override -> ~/.icmg/embed/all-MiniLM-L6-v2.onnx
        const char* env_model = std::getenv("ICMG_ONNX_MODEL");
        if (env_model && fs::exists(env_model)) {
            model_path_ = env_model;
            available_  = true;
            return;
        }
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (home) {
            fs::path p = fs::path(home) / ".icmg" / "embed" / "all-MiniLM-L6-v2.onnx";
            if (fs::exists(p)) { model_path_ = p.string(); available_ = true; }
        }
    }

    bool        available() const override { return available_; }
    int         dim() const override       { return 384; }
    std::string model() const override     { return "all-MiniLM-L6-v2"; }

    std::vector<float> embed(const std::string& text) override {
        // Phase 37: real impl path is here when ORT linkage works.
        // Algorithm sketch (filled when ORT lib usable):
        //   1. lazy-load model_ via Ort::Session
        //   2. tokenizer_.encode(text) -> input_ids + attention_mask
        //   3. session.Run(...) -> last_hidden_state [1, seq, 384]
        //   4. mask-weighted mean pool over tokens -> [384]
        //   5. L2-normalize, return
        //
        // Currently: returns {} so factory falls back to Python sidecar.
        // WordPiece tokenizer (Phase 34) ready and tested separately.
        (void)text;
        core::Logger::instance().warn(
            "ONNX backend: model found at " + model_path_ +
            " but real inference deferred (link ORT in Phase 37 dedicated session)."
        );
        return {};
    }

private:
    bool        available_  = false;
    std::string model_path_;
};

} // anon

// Visible to factory: returns ONNX impl if compiled + model found.
std::unique_ptr<Embedder> makeOnnxEmbedder() {
    auto e = std::make_unique<OnnxEmbedderStub>();
    if (!e->available()) return nullptr;
    return e;
}

} // namespace icmg::embed

#endif // ICMG_HAS_ONNX

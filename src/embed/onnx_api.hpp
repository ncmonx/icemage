#pragma once
// 2026-10-31: shared ONNX Runtime C-API accessor (embedder_onnx + reranker_onnx).
// Compiled only when ICMG_HAS_ONNX is defined.
//
// v1.57 S3: version-walk fallback. The vendored headers declare
// ORT_API_VERSION=25 (ORT 1.22+) but the bundled onnxruntime.dll is 1.17.1
// (supports up to API 17). GetApi(25) on that DLL returns nullptr and the
// next deref crashes with "API version [25] not available". Request the
// header version first, then walk DOWN until the DLL returns a non-null
// table. Callers only use ORT functions present since API 11
// (CreateEnv/CreateSession/Run/tensor helpers), so a v17 table is safe.
#ifdef ICMG_HAS_ONNX

#include <onnxruntime_c_api.h>

namespace icmg::embed {

inline const OrtApi* ortApi() {
    static const OrtApi* api = [] () -> const OrtApi* {
        const OrtApiBase* base = OrtGetApiBase();
        if (!base) return nullptr;
        for (uint32_t v = ORT_API_VERSION; v >= 1; --v) {
            if (const OrtApi* a = base->GetApi(v)) return a;
        }
        return nullptr;
    }();
    return api;
}

}  // namespace icmg::embed

#endif  // ICMG_HAS_ONNX

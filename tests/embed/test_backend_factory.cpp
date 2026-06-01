// Phase 33 — embedder factory env precedence + cmake-flag plumbing.
// Tests the policy logic, not actual model loading.
#include "../test_main.hpp"
#include <string>
#include <cstdlib>

// Mirror the factory's policy decision tree (without subprocess spawn).
enum class Choice { Onnx, Python, None };

struct PolicyArgs {
    bool has_onnx_compiled;
    bool onnx_available;
    bool python_available;
    std::string env_prefer;   // "onnx" | "python" | ""
};

static Choice decide(const PolicyArgs& a) {
    if (a.has_onnx_compiled) {
        if (a.env_prefer == "onnx" || a.env_prefer.empty()) {
            if (a.onnx_available) return Choice::Onnx;
            if (a.env_prefer == "onnx") return Choice::None;   // explicit-fail
        }
    } else {
        if (a.env_prefer == "onnx") return Choice::None;       // not compiled
    }
    if (a.env_prefer == "python" || a.env_prefer.empty()) {
        if (a.python_available) return Choice::Python;
    }
    return Choice::None;
}

TEST("factory: default no-onnx -> python available picks python") {
    PolicyArgs a{false, false, true, ""};
    ASSERT_TRUE(decide(a) == Choice::Python);
}

TEST("factory: default no-onnx + python missing -> none") {
    PolicyArgs a{false, false, false, ""};
    ASSERT_TRUE(decide(a) == Choice::None);
}

TEST("factory: env=onnx + not compiled -> none (loud fail)") {
    PolicyArgs a{false, false, true, "onnx"};
    ASSERT_TRUE(decide(a) == Choice::None);
}

TEST("factory: env=onnx + compiled + available -> onnx") {
    PolicyArgs a{true, true, true, "onnx"};
    ASSERT_TRUE(decide(a) == Choice::Onnx);
}

TEST("factory: env=onnx + compiled but model missing -> none") {
    PolicyArgs a{true, false, true, "onnx"};
    ASSERT_TRUE(decide(a) == Choice::None);
}

TEST("factory: env=python forces python even when onnx available") {
    PolicyArgs a{true, true, true, "python"};
    ASSERT_TRUE(decide(a) == Choice::Python);
}

TEST("factory: auto prefers onnx when both available") {
    PolicyArgs a{true, true, true, ""};
    ASSERT_TRUE(decide(a) == Choice::Onnx);
}

TEST("factory: auto falls to python when onnx model missing") {
    PolicyArgs a{true, false, true, ""};
    ASSERT_TRUE(decide(a) == Choice::Python);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif

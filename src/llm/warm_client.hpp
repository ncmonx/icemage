#pragma once
#include "llama_runner.hpp"
#include <chrono>
#include <optional>
#include <string>

namespace icmg::llm {

struct WarmInferResult {
    std::string text;
    int tok_in = 0;
    int tok_out = 0;
    int wall_ms = 0;
};

std::optional<WarmInferResult> tryWarmInfer(
    const std::string& prompt,
    const InferParams& opts,
    std::chrono::milliseconds connect_timeout = std::chrono::milliseconds(100));

bool warmAvailable();
std::string warmPipeName();

}

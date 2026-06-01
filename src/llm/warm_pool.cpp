// v1.31.0 B3a: in-process LLM warm-pool impl. See warm_pool.hpp.
#include "warm_pool.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace icmg::llm {

namespace {

fs::path llmDir() {
    const char* home =
#ifdef _WIN32
        std::getenv("USERPROFILE");
#else
        std::getenv("HOME");
#endif
    fs::path p = (home && *home) ? fs::path(home) : fs::current_path();
    return p / ".icmg" / "llm";
}

std::string readActive(const fs::path& dir) {
    std::ifstream f(dir / "active");
    if (!f) return "";
    std::string s; std::getline(f, s);
    return s;
}

bool isDisabled(const fs::path& dir) {
    std::error_code ec;
    return fs::exists(dir / "disabled", ec);
}

std::uint64_t minRamForModel(const fs::path& dir, const std::string& id) {
    std::ifstream f(dir / "registry.json");
    if (!f) return 0;
    nlohmann::json j;
    try { f >> j; } catch (...) { return 0; }
    for (const auto& m : j.value("models", nlohmann::json::array())) {
        if (m.value("id", std::string{}) == id) {
            return m.value("min_ram_mb", static_cast<std::uint64_t>(0));
        }
    }
    return 0;
}

} // namespace

WarmPool& WarmPool::instance() {
    static WarmPool wp;
    return wp;
}

LlamaRunner* WarmPool::acquire(std::string& err) {
    std::lock_guard<std::mutex> g(mu_);
    if (!LlamaRunner::available()) {
        err = "build lacks ICMG_USE_LLAMA";
        return nullptr;
    }
    fs::path dir = llmDir();
    if (isDisabled(dir)) {
        err = "user opt-out (~/.icmg/llm/disabled)";
        return nullptr;
    }
    std::string active = readActive(dir);
    if (active.empty()) {
        err = "no active model — `icmg llm use <id>`";
        return nullptr;
    }
    if (runner_ && active_id_ == active) {
        return runner_.get();
    }
    // Model changed or first load — (re)build runner.
    runner_.reset(new LlamaRunner());
    fs::path gguf = dir / active / "model.gguf";
    std::error_code ec;
    if (!fs::exists(gguf, ec)) {
        err = "model not installed: " + gguf.string();
        runner_.reset();
        return nullptr;
    }
    std::uint64_t min_ram = minRamForModel(dir, active);
    if (!runner_->load(gguf.string(), {}, min_ram)) {
        err = "load failed: " + runner_->lastError();
        runner_.reset();
        return nullptr;
    }
    active_id_ = active;
    return runner_.get();
}

bool WarmPool::isLoaded() const {
    std::lock_guard<std::mutex> g(mu_);
    return runner_ && runner_->isLoaded();
}

std::string WarmPool::activeModelId() const {
    std::lock_guard<std::mutex> g(mu_);
    return active_id_;
}

void WarmPool::release() {
    std::lock_guard<std::mutex> g(mu_);
    runner_.reset();
    active_id_.clear();
}

} // namespace icmg::llm

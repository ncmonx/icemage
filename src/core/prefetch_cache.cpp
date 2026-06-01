// v1.18.0: prefetch cache impl.
//
// On warm(): runs registered cmds in-process (no subprocess), captures stdout
// via rdbuf swap, stores result. Subsequent get*() return cached string.

#include "prefetch_cache.hpp"
#include "registry.hpp"
#include "../cli/base_command.hpp"

#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <streambuf>

namespace icmg::core::prefetch_cache {

namespace {

std::mutex g_mu;
std::string g_hot_context;
std::string g_skill_manifest;
std::vector<MemoryEntry> g_recent_memory;
std::atomic<bool>    g_warm{false};
std::atomic<int64_t> g_warmed_at{0};

// Capture stdout of cmd run in-process.
std::string captureCmd(const std::string& cmd_name,
                       const std::vector<std::string>& argv) {
    std::ostringstream buf;
    auto* old_cout = std::cout.rdbuf(buf.rdbuf());
    std::ostringstream null_err;
    auto* old_cerr = std::cerr.rdbuf(null_err.rdbuf());
    try {
        auto& reg = Registry<cli::BaseCommand>::instance();
        auto cmd = reg.create(cmd_name);
        if (cmd) (void)cmd->run(argv);
    } catch (...) {}
    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);
    return buf.str();
}

}  // namespace

void warm() {
    if (g_warm.load()) return;  // idempotent: skip if already warmed
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_warm.load()) return;  // double-checked

    g_hot_context = captureCmd("context-node",
        {"match", "", "--tier", "hot", "--top", "5", "--fmt", "plain"});
    g_skill_manifest = captureCmd("skill", {"manifest"});
    // recent_memory: skip detailed load; can be lazy on first recall.
    // Avoids ORM-level join overhead at warm time. Subsequent recall hits
    // tier-1 cache built progressively.

    g_warmed_at.store((int64_t)std::time(nullptr));
    g_warm.store(true);
}

std::string hotContextNodes() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_hot_context;
}

std::string skillManifest() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_skill_manifest;
}

std::vector<MemoryEntry> recentMemory() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_recent_memory;
}

void invalidate() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_hot_context.clear();
    g_skill_manifest.clear();
    g_recent_memory.clear();
    g_warm.store(false);
}

bool isWarm() { return g_warm.load(); }
int64_t lastWarmedAt() { return g_warmed_at.load(); }

}  // namespace icmg::core::prefetch_cache

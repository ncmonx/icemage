#include "session_state.hpp"
#include <fstream>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <cstdlib>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::core {

static int64_t nowSecs() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string stateFilePath() {
    const char* h = std::getenv("USERPROFILE");
    if (!h) h = std::getenv("HOME");
    if (!h) h = ".";
    return std::string(h) + "/.icmg/session-seen.json";
}

SessionState& SessionState::instance() {
    static SessionState inst;
    return inst;
}

SessionState::SessionState() : path_(stateFilePath()) { load(); }

void SessionState::load() {
    try {
        std::ifstream f(path_);
        if (!f) return;
        auto j = json::parse(f);
        int64_t ca = j.value("created_at", (int64_t)0);
        if (nowSecs() - ca > TTL_SECS) return; // expired — start fresh
        created_at_ = ca;
        for (auto& s : j.value("seen", json::array()))
            seen_.insert(s.get<std::string>());
    } catch (...) {}
}

void SessionState::save() {
    try {
        fs::create_directories(fs::path(path_).parent_path());
        if (created_at_ == 0) created_at_ = nowSecs();
        json j;
        j["created_at"] = created_at_;
        json arr = json::array();
        for (auto& s : seen_) arr.push_back(s);
        j["seen"] = arr;
        std::ofstream f(path_);
        f << j.dump();
    } catch (...) {}
}

bool SessionState::hasSeen(const std::string& key) const {
    return seen_.count(key) > 0;
}

void SessionState::markSeen(const std::string& key) {
    if (created_at_ == 0) created_at_ = nowSecs();
    seen_.insert(key);
    save();
}

void SessionState::clear() {
    seen_.clear();
    created_at_ = 0;
    try { fs::remove(path_); } catch (...) {}
}

} // namespace icmg::core

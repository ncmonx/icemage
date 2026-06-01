#include "ref_registry.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

// FNV1a 64-bit — fast, no deps, good enough for content keying.
std::string RefRegistry::contentHash(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    std::ostringstream os;
    os << std::hex << h;
    return os.str();
}

RefRegistry::RefRegistry(const std::string& project_root) {
    // Day bucket: same day = same session ID.
    time_t now = std::time(nullptr);
    int day = (int)(now / 86400);
    std::string day_str = std::to_string(day);
    std::string sid = contentHash(project_root + ":" + day_str).substr(0, 12);
    fs::path p = fs::path(project_root) / ".icmg" / ("refs-" + sid + ".json");
    state_path_ = p.string();
    try {
        fs::create_directories(p.parent_path());
        active_ = true;
        load();
    } catch (...) {
        active_ = false;
    }
}

RefRegistry::~RefRegistry() {
    if (active_ && dirty_) flush();
}

void RefRegistry::load() {
    if (!fs::exists(state_path_)) return;
    try {
        std::ifstream f(state_path_);
        json j; f >> j;
        if (j.contains("refs") && j["refs"].is_object()) {
            for (auto& [k, v] : j["refs"].items()) hash_to_id_[k] = v.get<std::string>();
        }
        if (j.contains("counters") && j["counters"].is_object()) {
            for (auto& [k, v] : j["counters"].items()) next_id_[k] = v.get<int>();
        }
    } catch (...) {}
}

void RefRegistry::flush() {
    if (!active_) return;
    try {
        json j;
        j["refs"] = json::object();
        for (auto& [k, v] : hash_to_id_) j["refs"][k] = v;
        j["counters"] = json::object();
        for (auto& [k, v] : next_id_) j["counters"][k] = v;
        std::ofstream f(state_path_);
        f << j.dump(2);
        dirty_ = false;
    } catch (...) {}
}

std::string RefRegistry::getOrAssign(const std::string& kind, const std::string& content) {
    if (!active_) return "";
    std::string key = kind + ":" + contentHash(content);
    auto it = hash_to_id_.find(key);
    if (it != hash_to_id_.end()) return it->second;
    int n = ++next_id_[kind];
    std::string id = "[ICMG-" + kind + "-" + std::to_string(n) + "]";
    hash_to_id_[key] = id;
    dirty_ = true;
    return id;
}

bool RefRegistry::seen(const std::string& kind, const std::string& content) const {
    if (!active_) return false;
    std::string key = kind + ":" + contentHash(content);
    return hash_to_id_.find(key) != hash_to_id_.end();
}

} // namespace icmg::cli

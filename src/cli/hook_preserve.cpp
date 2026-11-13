#include "hook_preserve.hpp"

#include "../core/atomic_file.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <sstream>

namespace icmg::cli {

namespace fs = std::filesystem;

std::string hookHash(const std::string& content) {
    std::uint64_t h = 1469598103934665603ULL;          // FNV-1a offset basis
    for (unsigned char c : content) {
        h ^= c;
        h *= 1099511628211ULL;                         // FNV-1a prime
    }
    static const char* kHex = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[i] = kHex[h & 0xF]; h >>= 4; }
    return out;
}

HookAction decideHookAction(bool exists, const std::string& current,
                            const std::string& tmpl,
                            const std::string& recorded_hash, bool overwrite) {
    if (!exists) return HookAction::Create;
    if (current == tmpl) return HookAction::Unchanged;
    if (!recorded_hash.empty() && hookHash(current) == recorded_hash)
        return HookAction::Upgrade;                    // pristine older template
    if (overwrite) return HookAction::BackupAndWrite;
    if (!recorded_hash.empty()) return HookAction::KeepUser;
    return HookAction::BackupAndWrite;                 // legacy: no record yet
}

std::map<std::string, std::string> loadHookManifest(const fs::path& claude_dir) {
    std::map<std::string, std::string> m;
    std::ifstream in(claude_dir / kHookManifestName, std::ios::binary);
    if (!in) return m;
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        auto j = nlohmann::json::parse(ss.str());
        if (j.is_object() && j.contains("files") && j["files"].is_object())
            for (auto& [k, v] : j["files"].items())
                if (v.is_string()) m[k] = v.get<std::string>();
    } catch (...) {
        m.clear();                                     // corrupt -> treat as legacy
    }
    return m;
}

bool saveHookManifest(const fs::path& claude_dir,
                      const std::map<std::string, std::string>& m) {
    nlohmann::json j;
    j["version"] = 1;
    j["note"] = "Hashes of hook files as last written by icmg init. "
                "Edited hooks are preserved on refresh (issue #281).";
    j["files"] = nlohmann::json::object();
    for (auto& [k, v] : m) j["files"][k] = v;
    std::string err;
    return core::writeFileAtomic(claude_dir / kHookManifestName, j.dump(2) + "\n", &err);
}

}  // namespace icmg::cli

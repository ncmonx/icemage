#pragma once
// WASM skill manifest + capability model (PURE — no I/O, no wasmtime).
// A registered WASM skill = this manifest (stored as a profile-store entry,
// kind="wasm") + the .wasm bytes. filter-v1 ABI = text-in / text-out.
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>

namespace icmg::wasm {

struct WasmSkill {
    std::string name, kind, match, wasmPath, abi, sha256;
    std::vector<std::string> caps;
};

// ABIs this build understands. filter-v1 = (icmg_alloc, icmg_filter, memory).
inline bool knownAbi(const std::string& abi) { return abi == "filter-v1"; }

// Parse + validate a manifest. Returns nullopt + err on any problem.
inline std::optional<WasmSkill> parseSkillManifest(const std::string& json,
                                                   std::string& err) {
    err.clear();
    nlohmann::json j;
    try { j = nlohmann::json::parse(json); }
    catch (const std::exception& e) { err = std::string("json: ") + e.what(); return std::nullopt; }

    auto req = [&](const char* k, std::string& out) -> bool {
        if (!j.contains(k) || !j[k].is_string()) {
            err = std::string("missing/non-string field: ") + k;
            return false;
        }
        out = j[k].get<std::string>();
        return true;
    };

    WasmSkill s;
    if (!req("name", s.name) || !req("kind", s.kind) || !req("match", s.match)
        || !req("wasm", s.wasmPath) || !req("abi", s.abi) || !req("sha256", s.sha256))
        return std::nullopt;

    if (!knownAbi(s.abi)) { err = "unknown abi: " + s.abi; return std::nullopt; }

    if (j.contains("capabilities") && j["capabilities"].is_array())
        for (auto& c : j["capabilities"])
            if (c.is_string()) s.caps.push_back(c.get<std::string>());

    return s;
}

// Pure: granted = declared INTERSECT allowlist. Over-declared / unknown dropped.
// Empty allowlist (default) denies all -> a skill runs as a pure function.
inline std::vector<std::string> grantedCaps(const std::vector<std::string>& declared,
                                            const std::vector<std::string>& allowlist) {
    std::vector<std::string> out;
    for (const auto& d : declared)
        if (std::find(allowlist.begin(), allowlist.end(), d) != allowlist.end())
            out.push_back(d);
    return out;
}

} // namespace icmg::wasm

#pragma once
// extractor-v1 ABI: WASM-backed language extractor manifest + output mapper.
// PURE — no I/O, no wasmtime. Phase 1 of WASM extractor modules (2026-07-01).
//
// A registered WASM extractor = this manifest (profile-store entry, kind="extractor")
// + the .wasm bytes. extractor-v1 ABI (module exports, mirrors filter-v1):
//     (memory)                                  - exported linear memory
//     int32   icmg_alloc(int32 size)            - alloc input buffer, returns ptr
//     int64   icmg_extract(int32 ptr, int32 len)- run; returns packed (outPtr<<32 | outLen)
// Output at [outPtr, outPtr+outLen) in linear memory is a UTF-8 JSON string.
// Host imports exposed to the sandbox (pure + observability only):
//     void    icmg_log(int32 msg_ptr, int32 msg_len)   [optional]
// Output JSON contract (maps 1:1 to graph::ExtractResult):
//     { "context":str, "imports":[str], "namespaces":[str],
//       "classes":[str], "functions":[str], "tables":[str] }
#include "../graph/extractor/base_extractor.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace icmg::wasm {

struct WasmExtractor {
    std::string name, language, wasmPath, abi, sha256, minIcmg;
    std::vector<std::string> extensions;   // e.g. {".zig"}
    int priority = 0;                       // > builtin priority wins arbitration
};

// ABIs this build understands for extractor skills.
inline bool knownExtractorAbi(const std::string& abi) { return abi == "extractor-v1"; }

// Semver floor gate: true if `cur` >= `floor` (major.minor.patch). Empty floor
// = no constraint. Non-numeric / missing components treated as 0. Pure.
inline bool icmgVersionAtLeast(const std::string& cur, const std::string& floor) {
    if (floor.empty()) return true;
    auto parse = [](const std::string& v, int out[3]) {
        out[0] = out[1] = out[2] = 0;
        int idx = 0; size_t i = 0;
        while (i < v.size() && idx < 3) {
            long n = 0; bool any = false;
            while (i < v.size() && v[i] >= '0' && v[i] <= '9') { n = n * 10 + (v[i]-'0'); any = true; ++i; }
            if (any) out[idx] = static_cast<int>(n);
            ++idx;
            if (i < v.size() && v[i] == '.') ++i; else break;
        }
    };
    int c[3], f[3];
    parse(cur, c); parse(floor, f);
    for (int i = 0; i < 3; ++i) {
        if (c[i] > f[i]) return true;
        if (c[i] < f[i]) return false;
    }
    return true; // equal
}

// Parse + validate an extractor manifest. Returns nullopt + err on any problem.
inline std::optional<WasmExtractor> parseExtractorManifest(const std::string& json,
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

    WasmExtractor e;
    if (!req("name", e.name) || !req("abi", e.abi) || !req("language", e.language)
        || !req("wasm", e.wasmPath) || !req("sha256", e.sha256))
        return std::nullopt;

    if (!knownExtractorAbi(e.abi)) { err = "unknown abi: " + e.abi; return std::nullopt; }

    if (j.contains("extensions") && j["extensions"].is_array())
        for (auto& x : j["extensions"])
            if (x.is_string()) e.extensions.push_back(x.get<std::string>());

    if (j.contains("priority") && j["priority"].is_number_integer())
        e.priority = j["priority"].get<int>();

    if (j.contains("min_icmg") && j["min_icmg"].is_string())
        e.minIcmg = j["min_icmg"].get<std::string>();

    return e;
}

// Map an extractor-v1 output JSON string into graph::ExtractResult.
// Returns false + err on parse failure. Missing fields default to empty.
// Non-string array entries are skipped defensively (untrusted sandbox output).
inline bool parseExtractorOutput(const std::string& json,
                                 graph::ExtractResult& out, std::string& err) {
    err.clear();
    nlohmann::json j;
    try { j = nlohmann::json::parse(json); }
    catch (const std::exception& e) { err = std::string("json: ") + e.what(); return false; }
    if (!j.is_object()) { err = "output not a JSON object"; return false; }

    if (j.contains("context") && j["context"].is_string())
        out.context = j["context"].get<std::string>();

    auto arr = [&](const char* k, std::vector<std::string>& dst) {
        if (j.contains(k) && j[k].is_array())
            for (auto& x : j[k])
                if (x.is_string()) dst.push_back(x.get<std::string>());
    };
    arr("imports",    out.imports);
    arr("namespaces", out.namespaces);
    arr("classes",    out.classes);
    arr("functions",  out.functions);
    arr("tables",     out.tables);
    return true;
}

} // namespace icmg::wasm

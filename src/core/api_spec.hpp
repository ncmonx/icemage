#pragma once
// v2.0.0 externals (API Spec Compilation): compress a verbose OpenAPI JSON into a
// dense endpoint map — "METHOD /path — summary (N params)" — so an agent reads an
// API surface in a fraction of the tokens. Pure: JSON text in, compact text out.
#include <nlohmann/json.hpp>
#include <cctype>
#include <string>

namespace icmg::core {

inline std::string compactOpenApi(const std::string& jsonText) {
    using nlohmann::json;
    json j;
    try { j = json::parse(jsonText); }
    catch (...) { return ""; }
    if (!j.contains("paths") || !j["paths"].is_object()) return "";

    static const char* kVerbs[] = {"get","post","put","patch","delete","head","options"};
    std::string out;
    for (auto it = j["paths"].begin(); it != j["paths"].end(); ++it) {
        const std::string& path = it.key();
        const json& methods = it.value();
        if (!methods.is_object()) continue;
        for (const char* v : kVerbs) {
            if (!methods.contains(v)) continue;
            const json& op = methods[v];
            std::string M(v);
            for (auto& c : M) c = (char)std::toupper((unsigned char)c);
            out += M + " " + path;
            if (op.is_object() && op.contains("summary") && op["summary"].is_string())
                out += " — " + op["summary"].get<std::string>();
            if (op.is_object() && op.contains("parameters") && op["parameters"].is_array()
                && !op["parameters"].empty())
                out += " (" + std::to_string(op["parameters"].size()) + " params)";
            out += "\n";
        }
    }
    return out;
}

}  // namespace icmg::core

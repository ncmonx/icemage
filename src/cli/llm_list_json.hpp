#pragma once
// v1.70.0 (#177): `icmg llm list` must emit PURE JSON.
//
// The old cmdList dumped the registry file then appended a trailing
// `active: <id>` plain-text line, so the combined output failed `json::parse`
// (trailing content) and the active model lived only in the text line, not the
// JSON. buildLlmListJson parses the registry, injects an "active" key, and
// emits a single clean JSON document (crash-safe via safeDump).
#include "../core/json_safe.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace icmg::cli {

inline std::string buildLlmListJson(const std::string& registry_json,
                                    const std::string& active) {
    nlohmann::json j = nlohmann::json::parse(registry_json, nullptr,
                                             /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        // Registry unparseable — still emit valid JSON carrying the active id.
        nlohmann::json o;
        o["active"] = active;
        o["models"] = nlohmann::json::array();
        return icmg::core::safeDump(o, 2);
    }
    j["active"] = active;   // single source of truth, inside the JSON body
    return icmg::core::safeDump(j, 2);
}

} // namespace icmg::cli

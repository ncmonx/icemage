#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace icmg::mcp {

// MCP tool annotations (spec revision 2025-03-26+): behavioral HINTS a client
// uses for planning -- they are advisory, never a guarantee (per spec, clients
// must not rely on them for security). Four booleans + an optional title:
//   readOnlyHint    -- the tool does not modify its environment
//   destructiveHint -- may perform destructive updates (only meaningful when
//                      NOT read-only); icmg writes are additive, so default false
//   idempotentHint  -- repeated calls with the same args have no extra effect
//   openWorldHint   -- interacts with external entities (network/filesystem
//                      outside the project); local-first tools default false
struct McpToolAnnotations {
    std::string title;          // optional human-facing title ("" -> omitted)
    bool readOnly    = true;
    bool destructive = false;
    bool idempotent  = true;
    bool openWorld   = false;
};

// Derive accurate defaults from the tool's existing isMutating() signal, so
// every one of the 28 tools gets correct hints for free. Tools override
// annotations() to refine (e.g. fetch/ingest/sync set openWorld = true).
inline McpToolAnnotations defaultToolAnnotations(bool mutating) {
    McpToolAnnotations a;
    a.readOnly    = !mutating;   // read-only unless it writes state
    a.destructive = false;       // icmg writes are additive, not deletes
    a.idempotent  = !mutating;   // a read repeats safely; a write may not
    a.openWorld   = false;       // local-first by default
    return a;
}

// Serialize to the MCP `annotations` object. Emits all four hints; title only
// when set (spec allows omission, and an empty title is meaningless).
inline nlohmann::json toolAnnotationsToJson(const McpToolAnnotations& a) {
    nlohmann::json j = {
        {"readOnlyHint",    a.readOnly},
        {"destructiveHint", a.destructive},
        {"idempotentHint",  a.idempotent},
        {"openWorldHint",   a.openWorld},
    };
    if (!a.title.empty()) j["title"] = a.title;
    return j;
}

} // namespace icmg::mcp

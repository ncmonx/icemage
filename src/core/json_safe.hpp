#pragma once
// v1.68.1: crash-safe JSON serialization.
//
// nlohmann::json::dump() THROWS type_error.316 when the json holds a string
// with invalid UTF-8 (e.g. memory/graph content that captured raw binary such
// as a PNG header byte 0x89). When such a dump runs outside a try/catch — as in
// the `icmg hook userprompt` augmented-prompt path — the throw is uncaught and
// the process abort()s, taking down the whole hook.
//
// safeDump uses error_handler_t::replace, which substitutes U+FFFD for invalid
// sequences instead of throwing. This is the same mitigation already applied at
// the MCP server (src/mcp/server.cpp) and sync push (src/cli/commands/sync_cmd.cpp);
// this header centralizes it so every call site is one short, obvious call.
#include <nlohmann/json.hpp>
#include <string>

namespace icmg::core {

// Serialize j without ever throwing on invalid UTF-8 (replaces with U+FFFD).
inline std::string safeDump(const nlohmann::json& j, int indent = -1) {
    return j.dump(indent, ' ', /*ensure_ascii=*/false,
                  nlohmann::json::error_handler_t::replace);
}

} // namespace icmg::core

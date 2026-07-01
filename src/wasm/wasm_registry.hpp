#pragma once
// Look up registered WASM skills (persona DB, zone "wasm") matching a command.
#include "wasm_skill.hpp"
#include "wasm_extractor.hpp"
#include <optional>
#include <string>
#include <vector>

namespace icmg::wasm {

constexpr const char* WASM_ZONE = "wasm";

// Pure: does a skill's match-pattern apply to this command? (substring).
inline bool matchesCommand(const std::string& match, const std::string& command) {
    if (match.empty()) return false;
    return command.find(match) != std::string::npos;
}

// First registered WASM skill whose match fits `command`. Fail-open:
// returns nullopt on no persona DB / no match / any error (never throws).
std::optional<WasmSkill> matchWasmSkill(const std::string& command);

// All registered WASM extractors (persona DB rows with kind=="extractor").
// Fail-open: returns {} on no persona DB / any error (never throws).
std::vector<WasmExtractor> loadWasmExtractors();

} // namespace icmg::wasm

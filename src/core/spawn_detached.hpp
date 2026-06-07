// 2026-06-06: fire-and-forget detached process launch (no wait, no capture).
// Used by auto-consolidate (#6) to run `icmg memory consolidate` in the
// background without blocking the foreground store.
#pragma once
#include <string>
#include <vector>

namespace icmg::core {
// Launch argv[0] with argv[1..] detached. Returns true if the spawn was issued
// (not whether the child succeeded). Caller must not depend on completion.
bool spawnDetached(const std::vector<std::string>& argv);
}

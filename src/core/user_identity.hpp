// Phase 47 T4: user identity resolution.
// Priority: ICMG_USER env > git config user.email > "anonymous"
#pragma once
#include <string>

namespace icmg::core {

// Resolves once per process; cached static.
const std::string& currentUser();

} // namespace icmg::core

// Phase 50 T3: validate URL string before passing to shell-out (curl).
// Rejects shell metacharacters that could enable command injection.
#pragma once
#include <string>

namespace icmg::core {

// Returns true if URL is safe to embed in shell-quoted curl invocation.
// Allowed: RFC 3986 + tolerant of practical URL chars.
// Rejected: shell metacharacters (" ` $ \ ; | & \n \r) and control chars.
bool isUrlSafe(const std::string& url);

// Validates + sets reason on failure. Use for error messages.
bool validateUrl(const std::string& url, std::string& reason);

} // namespace icmg::core

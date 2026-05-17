#pragma once
// T15a: Secretlint — scan text for common secret patterns, redact matches.
#include <string>
#include <vector>

namespace icmg::core {

struct SecretMatch {
    std::string type;   // e.g. "AWS_ACCESS_KEY"
    std::string match;  // matched substring
    size_t      offset; // byte offset in source text
};

// Scan text for known secret patterns. Returns all matches found.
std::vector<SecretMatch> scanSecrets(const std::string& text);

// Replace each match with <REDACTED:TYPE>. Processes matches in reverse
// offset order so earlier offsets stay valid after substitution.
std::string redactSecrets(const std::string& text,
                          const std::vector<SecretMatch>& matches);

} // namespace icmg::core

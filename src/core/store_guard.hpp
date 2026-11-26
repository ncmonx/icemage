#pragma once
// #292: secret / PII filter on store. Every memory write (MemoryStore::store)
// and every knowledge-node write (ContextNodeStore::upsert) runs its text
// through scanForStore() before it is persisted.
//
// Mode (first set wins): $ICMG_SECRET_FILTER, config key "secret_filter",
// default kDefaultSecretMode.
//   reject -> the write fails with SecretRejectedError, nothing is stored
//   mask   -> each match becomes <REDACTED:TYPE>, a notice goes to stderr
//   warn   -> stored as-is, a notice goes to stderr
//   off    -> no scan
//
// Patterns: the built-in secret scanner (secret_scanner.hpp: cloud/API keys,
// JWT, PEM keys), plus store-only rules for password/token assignments,
// credentials in URLs, bearer tokens, US SSN and card numbers (Luhn-checked),
// plus user rules: one regex per line, optionally `NAME=regex`, `#` comments,
// read from <project>/.icmg/secret-patterns.txt and the file named by
// $ICMG_SECRET_PATTERNS. National ID formats differ per country, so they are
// left to the user file.
#include "secret_scanner.hpp"
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace icmg::core {

enum class SecretMode { Off, Warn, Mask, Reject };

inline constexpr const char* kSecretModeEnv      = "ICMG_SECRET_FILTER";
inline constexpr const char* kSecretModeConfig   = "secret_filter";
inline constexpr const char* kSecretPatternsEnv  = "ICMG_SECRET_PATTERNS";
inline constexpr const char* kSecretPatternsFile = ".icmg/secret-patterns.txt";
inline constexpr SecretMode  kDefaultSecretMode  = SecretMode::Mask;

class SecretRejectedError : public std::runtime_error {
public:
    explicit SecretRejectedError(const std::string& msg) : std::runtime_error(msg) {}
};

// "reject" | "mask" | "warn" | "off" (case-insensitive). *ok=false on others.
SecretMode parseSecretMode(std::string_view s, bool* ok);
const char* secretModeName(SecretMode m);
SecretMode activeSecretMode();

// All matches in `text`: built-in + store-only + user patterns, by offset.
std::vector<SecretMatch> scanForStore(const std::string& text);

// Applies `mode` to each field in place. In Mask mode every field is also
// scrubbed of tokens taken from a secret found in ANY field (#294: auto
// keywords split "password=X" into "password,x", losing the pattern context).
// Returns the distinct match types
// found (empty = clean). Throws SecretRejectedError in Reject mode when
// anything matched. `what` names the write in notices ("store", "knowledge").
std::vector<std::string> guardFields(const std::vector<std::string*>& fields,
                                     SecretMode mode, const char* what);

// Same, with activeSecretMode().
std::vector<std::string> guardFields(const std::vector<std::string*>& fields, const char* what);

// #294: `text` with every secret span blanked out (never the secret, never a
// placeholder). Feed THIS to derived-field builders (auto keywords, titles) so
// they can't pick up a secret. Uses scanForStore regardless of mode.
std::string keywordSourceText(const std::string& text);

// Tests: replace the user pattern list (lines in the file format above);
// an empty list re-enables loading from disk.
void setUserSecretPatternsForTest(const std::vector<std::string>& lines);

}  // namespace icmg::core

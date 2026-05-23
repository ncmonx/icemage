// v1.27.0 (Phase 1.2): Public surface for ICMG-PORT v1 artifact format.
//
// Extracted from port_cmd.cpp anon namespace to enable unit tests against
// the serialize/parse helpers. Format unchanged from v1.24.0.

#pragma once

#include <string>

namespace icmg::cli::port_artifact {

// FNV-1a 128-bit (two parallel 64-bit streams). NOT crypto — corruption-
// detection only. Matches core/audit_log.cpp approach for consistency.
std::string fnv128hex(const std::string& msg);

struct ArtifactParse {
    bool ok = false;
    std::string error;
    int files = 0;
    long long raw_bytes = 0;
    std::string hash;
    std::string payload;
};

// Parse blob starting with `ICMG-PORT v1\n` magic, header K:V lines, `\n---\n`
// separator, then payload. Verifies fnv128hex(payload) == HASH header.
// On failure: ok=false, error populated, payload may be partial.
ArtifactParse parseArtifact(const std::string& blob);

// Serialize JSON-string payload into ICMG-PORT v1 wire format.
// `name` becomes the BUNDLE header value, `files` and `raw_bytes` are
// FILES/RAW header values, payload is appended after `\n---\n` separator.
std::string serializeArtifact(const std::string& name,
                               int files,
                               long long raw_bytes,
                               const std::string& payload);

}  // namespace icmg::cli::port_artifact

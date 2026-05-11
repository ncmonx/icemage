// Phase 75: HMAC-chained append-only audit log.
//
// Every entry: ISO-ts | actor | event | payload | prev_hmac | hmac
// hmac = sha256(secret_key || prev_hmac || row_payload)
// Tamper detection: replay verifies chain end-to-end.
//
// Key file: ~/.icmg/secret.key (mode 0600). Auto-created on first use.
// Log file: ~/.icmg/audit.log (or per-project .icmg/audit.log).
//
// NOT a security boundary against root user — secret.key readable by user.
// Detects accidental edits / corruption / malicious tampering by other users
// without root.

#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace icmg::core {

class AuditLog {
public:
    // path: full path to .icmg/audit.log (project-local) or
    //       ~/.icmg/audit.log (global). Created if missing.
    explicit AuditLog(const std::string& log_path,
                      const std::string& key_path = "");

    // Append entry. Returns hmac of new row (hex), or empty on failure.
    // payload should be a JSON-ish blob (no newlines).
    std::string append(const std::string& actor,
                       const std::string& event,
                       const std::string& payload);

    struct Entry {
        std::string ts;       // ISO-8601 UTC
        std::string actor;
        std::string event;
        std::string payload;
        std::string prev_hmac;
        std::string hmac;
        int64_t     line_no = 0;
    };

    // Read all entries (oldest first).
    std::vector<Entry> read(int limit = 0);

    // Verify chain integrity end-to-end. Returns count of bad rows; 0 = clean.
    // bad_rows (out): line numbers (1-based) where chain breaks.
    int verify(std::vector<int64_t>* bad_rows = nullptr);

    // Tail last N entries.
    std::vector<Entry> tail(int n);

private:
    std::string log_path_;
    std::string key_path_;
    std::string key_;            // raw bytes loaded from key_path_

    void ensureKey();
    static std::string hmac_sha256(const std::string& key,
                                   const std::string& msg);
    static std::string hexEncode(const std::string& bytes);
    static std::string nowIso();
};

} // namespace icmg::core

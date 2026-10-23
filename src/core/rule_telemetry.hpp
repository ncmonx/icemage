// v1.35.0 R4: rule violation telemetry. Backed by `rule_violations`
// table (migration 0027) in the global DB.
//
// Every PreToolUse leash `block` writes a violation row; subsequent
// leash invocations check escalation count and may convert warn→hard-
// block, or feed R8 auto-pin to prepend top-N most-violated rules
// to UserPromptSubmit header.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace icmg::core {

struct RuleViolationSummary {
    std::string rule_id;
    int         count_session = 0;   // within current session_id
    int         count_total   = 0;   // all time
    std::int64_t last_at      = 0;
    std::string last_ctx;
};

// v2.26.0: one raw violation row (leash audit trail: `icmg rule-viol list`).
struct RuleViolationRow {
    std::string rule_id;
    std::string session_id;   // ACTOR: agent session, not the human user
    std::string ctx;          // may embed "[action=<class>] " prefix
    std::int64_t at = 0;
};

class RuleTelemetry {
public:
    // Record a violation. Best-effort; swallows DB errors.
    // `session_id` empty -> uses USERPROFILE-derived fallback.
    static void record(const std::string& rule_id,
                       const std::string& session_id = "",
                       const std::string& ctx        = "");

    // True if this rule was violated >= threshold times in the given
    // session — triggers escalation to hard-block by leash.
    static bool isEscalated(const std::string& rule_id,
                            const std::string& session_id,
                            int threshold = 2);

    // Top-N rules by total violation count across all sessions (for R8 auto-pin).
    static std::vector<RuleViolationSummary> topByCount(int n = 5);

    // v2.26.0: most recent rows, newest first (actor-attributed audit trail).
    static std::vector<RuleViolationRow> recent(int limit = 20);

    // Clear all rows (admin reset).
    static int clearAll();
};

} // namespace icmg::core

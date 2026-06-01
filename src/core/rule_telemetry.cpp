// v1.35.0 R4: rule_telemetry impl. See rule_telemetry.hpp.
#include "rule_telemetry.hpp"
#include "global_db.hpp"
#include "user_identity.hpp"
#include "db.hpp"

#include <ctime>

namespace icmg::core {

namespace {

std::string sessionFallback(const std::string& provided) {
    if (!provided.empty()) return provided;
    // Stable per-user id (env ICMG_USER > git email > "anonymous").
    // Not strictly per-session — bounds escalation to per-user scope.
    try { return currentUser(); } catch (...) { return "anon"; }
}

} // namespace

void RuleTelemetry::record(const std::string& rule_id,
                           const std::string& session_id,
                           const std::string& ctx) {
    if (rule_id.empty()) return;
    try {
        GlobalDb& gdb = GlobalDb::instance();
        gdb.db().run(
            "INSERT INTO rule_violations(rule_id, session_id, ctx, occurred_at) "
            "VALUES(?, ?, ?, ?)",
            { rule_id, sessionFallback(session_id), ctx,
              std::to_string(static_cast<std::int64_t>(std::time(nullptr))) });
    } catch (...) {}
}

bool RuleTelemetry::isEscalated(const std::string& rule_id,
                                const std::string& session_id,
                                int threshold) {
    if (rule_id.empty() || threshold <= 0) return false;
    try {
        GlobalDb& gdb = GlobalDb::instance();
        int count = 0;
        gdb.db().query(
            "SELECT COUNT(*) FROM rule_violations WHERE rule_id = ? AND session_id = ?",
            { rule_id, sessionFallback(session_id) },
            [&](const Row& r){
                if (!r.empty()) { try { count = std::stoi(r[0]); } catch (...) {} }
            });
        return count >= threshold;
    } catch (...) { return false; }
}

std::vector<RuleViolationSummary> RuleTelemetry::topByCount(int n) {
    std::vector<RuleViolationSummary> out;
    if (n <= 0) return out;
    try {
        GlobalDb& gdb = GlobalDb::instance();
        gdb.db().query(
            "SELECT rule_id, COUNT(*) as c, MAX(occurred_at) as last, "
            "       (SELECT ctx FROM rule_violations rv2 WHERE rv2.rule_id = rv.rule_id "
            "         ORDER BY occurred_at DESC LIMIT 1) "
            "FROM rule_violations rv GROUP BY rule_id ORDER BY c DESC LIMIT " + std::to_string(n),
            {}, [&](const Row& r){
                if (r.size() < 4) return;
                RuleViolationSummary s;
                s.rule_id = r[0];
                try { s.count_total = std::stoi(r[1]); } catch (...) {}
                try { s.last_at     = std::stoll(r[2]); } catch (...) {}
                s.last_ctx = r[3];
                out.push_back(std::move(s));
            });
    } catch (...) {}
    return out;
}

int RuleTelemetry::clearAll() {
    try {
        GlobalDb& gdb = GlobalDb::instance();
        gdb.db().run("DELETE FROM rule_violations", {});
        return 0;
    } catch (...) { return 1; }
}

} // namespace icmg::core

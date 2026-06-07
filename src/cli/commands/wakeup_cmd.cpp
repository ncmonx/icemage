// Phase 26 T4: `icmg wake-up` — session-start briefing.
// Aggregates: last 5 high-importance memories, last 5 known-issue resolutions,
// last 3 memoirs touched, in-progress phases, failing verifications.
// Hard cap 2KB.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/user_identity.hpp"
#include "../../core/persona_db.hpp"          // #luna-batch: wake-up --resume
#include "../../core/profile_store.hpp"
#include "../resume_helpers.hpp"
#include "../session_greeting.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>
#include <chrono>

namespace icmg::cli {

class WakeUpCommand : public BaseCommand {
public:
    std::string name()        const override { return "wake-up"; }
    std::string description() const override { return "Session-start briefing (decisions, fixes, phases)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg wake-up [--since 7d] [--json] [--pack] [--resume]\n\n"
            "Briefing of recent decisions, fixes, in-progress phases.\n"
            "  --resume   also append persona identity + recent moments (continuity).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool json_out = hasFlag(args, "--json");
        bool pack     = hasFlag(args, "--pack");
        std::string since = flagValue(args, "--since", "7d");
        int64_t now = std::time(nullptr);
        int64_t cutoff = parseSince(since, now);

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        // User filter: ICMG_USER env > git config user.email > "anonymous".
        // "anonymous" = single-user machine → no filter (backward compat).
        const std::string& cur_user = core::currentUser();
        bool has_user = (cur_user != "anonymous" && !cur_user.empty());
        std::string user_filter = has_user ? " AND created_by = ?" : "";
        std::vector<std::string> user_param_extra = has_user ? std::vector<std::string>{cur_user} : std::vector<std::string>{};
        auto userParams = [&](std::vector<std::string> base) -> std::vector<std::string> {
            for (auto& p : user_param_extra) base.push_back(p);
            return base;
        };

        std::ostringstream out;
        if (pack) out << "# Wake-up bundle\n\n";
        out << "icmg wake-up — " << fmtDate(now) << "\n";
        // Authoritative wall-clock: this is already local time (fmtDate uses
        // localtime). Do NOT re-run `date` or re-convert the timezone — that is
        // how a 12:39 reading once became a wrong 05:39 (-7h TZ reconvert).
        out << "NOW (authoritative — trust this, do NOT re-run date / re-convert TZ): "
            << fmtDate(now) << "\n";

        // Gap-aware greeting: clearing the conversation is NOT a new day. Read
        // the last handoff's write time and tell the assistant whether to greet
        // as a CONTINUATION ("lanjut") or a FRESH session.
        {
            int64_t lastTs = 0; bool haveLast = false;
            try {
                namespace fs = std::filesystem;
                fs::path rp = fs::path(".") / ".remember" / "remember.md";
                if (fs::exists(rp)) {
                    auto ftime = fs::last_write_time(rp);
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
                    lastTs = (int64_t)std::chrono::system_clock::to_time_t(sctp);
                    haveLast = true;
                }
            } catch (...) {}
            auto hint = computeGreetingHint(now, lastTs, haveLast);
            if (hint.haveLast) {
                out << "Last handoff: " << fmtDate(lastTs)
                    << "  (gap " << formatGap(hint.gapSec) << ")  ->  greeting: "
                    << (hint.mode == "continue"
                            ? "CONTINUE (\"lanjut\" — same session-day, NOT a fresh morning)"
                            : "FRESH (greet by wall-clock time-of-day)")
                    << "\n";
            }
        }
        if (has_user) out << "[user: " << cur_user << "]\n";
        out << "(window: " << since << ")\n\n";

        // Top high-importance recent memories
        out << "Decisions (last 5):\n";
        int n = 0;
        db.query("SELECT id, topic, content, importance FROM memory_nodes "
                 "WHERE deleted_at IS NULL AND last_used > ? AND importance >= 2"
                 + user_filter + " ORDER BY last_used DESC LIMIT 5",
                 userParams({std::to_string(cutoff)}),
                 [&](const core::Row& r){
                     if (r.size() < 4) return;
                     out << "  [" << r[3] << "] " << trunc(r[1], 60) << "\n";
                     ++n;
                 });
        if (n == 0) out << "  (none in window)\n";

        // Known-issue
        out << "\nRecent fixes (last 5):\n";
        n = 0;
        db.query("SELECT topic, content FROM memory_nodes "
                 "WHERE deleted_at IS NULL AND topic LIKE 'errors-resolved%' "
                 "AND last_used > ?" + user_filter + " ORDER BY last_used DESC LIMIT 5",
                 userParams({std::to_string(cutoff)}),
                 [&](const core::Row& r){
                     if (r.size() < 2) return;
                     out << "  - " << trunc(r[1], 80) << "\n";
                     ++n;
                 });
        if (n == 0) out << "  (none in window)\n";

        // Memoirs touched
        out << "\nMemoirs touched (last 3):\n";
        n = 0;
        db.query("SELECT topic FROM memory_nodes "
                 "WHERE deleted_at IS NULL AND topic LIKE 'memoir:%' "
                 "AND last_used > ?" + user_filter + " ORDER BY last_used DESC LIMIT 3",
                 userParams({std::to_string(cutoff)}),
                 [&](const core::Row& r){
                     if (r.empty()) return;
                     out << "  - " << r[0].substr(7) << "\n";
                     ++n;
                 });
        if (n == 0) out << "  (none)\n";

        // In-progress phases (Phase 22 schema)
        out << "\nIn-progress phases:\n";
        n = 0;
        try {
            db.query("SELECT phase, name, started_at FROM phases "
                     "WHERE status='in-progress' ORDER BY phase DESC LIMIT 5", {},
                     [&](const core::Row& r){
                         if (r.size() < 3) return;
                         out << "  Phase " << r[0] << " — " << r[1] << "\n";
                         ++n;
                     });
        } catch (...) {}
        if (n == 0) out << "  (none)\n";

        // Failing verifications
        out << "\nFailing verifications:\n";
        n = 0;
        try {
            db.query("SELECT command, exit_code FROM verifications "
                     "WHERE exit_code != 0 AND created_at > ? "
                     "ORDER BY created_at DESC LIMIT 5",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (r.size() < 2) return;
                         out << "  - exit=" << r[1] << ": " << trunc(r[0], 80) << "\n";
                         ++n;
                     });
        } catch (...) {}
        if (n == 0) out << "  none\n";

        // Recall query stats
        int q_count = 0;
        try {
            db.query("SELECT COUNT(*) FROM query_history WHERE created_at > ?",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){ if (!r.empty()) q_count = std::stoi(r[0]); });
        } catch (...) {}
        out << "\nRecall queries in window: " << q_count << "\n";

        // #1130 T1: active sessions from ~/.icmg/active-work.json
        {
            namespace fs = std::filesystem;
            using nlohmann::json;
            fs::path awp = fs::path(cfg.globalDbPath()).parent_path() / "active-work.json";
            if (fs::exists(awp)) {
                try {
                    std::ifstream f(awp);
                    auto j = json::parse(f);
                    const auto& sessions = j["sessions"];
                    if (!sessions.empty()) {
                        out << "\nActive sessions:\n";
                        for (const auto& s2 : sessions) {
                            out << "  pid=" << s2.value("pid", int64_t{0})
                                << "  " << s2.value("task", std::string{}) << "\n";
                        }
                    }
                } catch (...) {}
            }
        }

        std::string s = out.str();
        if (s.size() > 2048) {
            s = s.substr(0, 2040) + "...\n";
        }

        // luna idea ("context --resume"): append persona identity + recent moments so a
        // fresh session re-hydrates continuity in one command. Appended AFTER the 2KB cap
        // so the persona block is never truncated. Fail-open (persona DB optional).
        if (hasFlag(args, "--resume") && core::personaDbAvailable()) {
            try {
                core::ProfileStore ps(core::personaDb());
                const std::string& u = core::currentUser();
                std::vector<std::pair<std::string,std::string>> identity;
                for (auto& r : ps.listZone(u, "_identity")) identity.emplace_back(r.key, r.content);
                std::vector<std::string> moments;
                for (auto& r : ps.listZone(u, "_moments")) moments.push_back(r.key);
                s += resumeSection(identity, moments);
            } catch (...) { /* fail-open: briefing still prints */ }
        }

        if (json_out) {
            // Simple JSON wrap (escaping minimal — output is short).
            std::string esc;
            for (char c : s) {
                if      (c == '"')  esc += "\\\"";
                else if (c == '\n') esc += "\\n";
                else if (c == '\\') esc += "\\\\";
                else                esc.push_back(c);
            }
            std::cout << "{\"briefing\":\"" << esc << "\"}\n";
        } else {
            std::cout << s;
        }
        return 0;
    }

private:
    static int64_t parseSince(const std::string& s, int64_t now) {
        if (s.empty()) return now - 7LL * 86400;
        char unit = s.back();
        int n = 0;
        try { n = std::stoi(s.substr(0, s.size() - 1)); } catch (...) { n = 7; }
        int64_t scale = 86400;
        if (unit == 'h') scale = 3600;
        else if (unit == 'm') scale = 60;
        else if (unit == 'w') scale = 7 * 86400;
        return now - (int64_t)n * scale;
    }
    static std::string trunc(const std::string& s, size_t n) {
        return s.size() <= n ? s : s.substr(0, n) + "...";
    }
    static std::string fmtDate(int64_t t) {
        char buf[32];
        time_t tt = (time_t)t;
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&tt));
        return buf;
    }
};

ICMG_REGISTER_COMMAND("wake-up", WakeUpCommand);

} // namespace icmg::cli

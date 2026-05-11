// Phase 75: `icmg drift` — anti-cognition-drift gate.
//
// Subcommands:
//   pin    --topic T --stance S [--rationale R] [--keywords K] [--actor A]
//                              Record decision; subsequent contradictions flagged
//   list   [--topic T] [--limit N]
//                              Show pinned anchors
//   check  <prompt-text>
//                              Match prompt against pinned decisions; exit 1 if
//                              contradictory keywords present (caller hook can
//                              inject warning to model)
//   supersede <new-id> --over <old-id>
//                              Mark old decision superseded; ONLY explicit override
//   report [--days N]
//                              Show stance reversals + repeated flips (drift markers)
//
// Designed to be called from UserPromptSubmit hook → injects warning context
// when prompt mentions a topic with pinned conflicting stance.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

class DriftCommand : public BaseCommand {
public:
    std::string name()        const override { return "drift"; }
    std::string description() const override {
        return "Decision anchors + drift detection (pin/list/check/supersede/report)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg drift <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  pin    --topic T --stance S [--rationale R] [--keywords K] [--actor A]\n"
            "                       Record decision anchor (pinned by default)\n"
            "  list   [--topic T] [--limit N] [--include-superseded]\n"
            "                       Show pinned decisions\n"
            "  check  <prompt-text> Match prompt against pinned stances; exit 1 if conflict\n"
            "  supersede <new-id> --over <old-id>\n"
            "                       Explicit override; old decision marked superseded\n"
            "  report [--days N]    Stance reversals + repeated flips (drift markers)\n\n"
            "Hook integration: UserPromptSubmit can call `icmg drift check $PROMPT`\n"
            "to inject contextual warning when user prompt contradicts a pinned anchor.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        if (sub == "pin")       return cmdPin(rest);
        if (sub == "list")      return cmdList(rest);
        if (sub == "check")     return cmdCheck(rest);
        if (sub == "supersede") return cmdSupersede(rest);
        if (sub == "report")    return cmdReport(rest);
        std::cerr << "icmg drift: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    static std::string lower(const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return out;
    }

    int cmdPin(const std::vector<std::string>& args) {
        std::string topic     = flagValue(args, "--topic");
        std::string stance    = flagValue(args, "--stance");
        std::string rationale = flagValue(args, "--rationale");
        std::string keywords  = flagValue(args, "--keywords");
        std::string actor     = flagValue(args, "--actor", "user");
        if (topic.empty() || stance.empty()) {
            std::cerr << "icmg drift pin: --topic and --stance required\n";
            return 1;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        db.run("INSERT INTO decisions(topic, stance, rationale, keywords, pinned, actor) "
               "VALUES (?, ?, ?, ?, 1, ?)",
               {topic, stance, rationale, keywords, actor});
        std::cout << "icmg drift: pinned (topic=" << topic
                  << ") id=" << db.lastInsertId() << "\n";
        return 0;
    }

    int cmdList(const std::vector<std::string>& args) {
        std::string only_topic = flagValue(args, "--topic");
        int limit = 20;
        try { limit = std::stoi(flagValue(args, "--limit", "20")); } catch (...) {}
        bool include_super = hasFlag(args, "--include-superseded");
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        std::string sql =
            "SELECT id, topic, stance, rationale, keywords, pinned, supersedes, "
            "       superseded_at, made_at, actor "
            "FROM decisions WHERE 1=1";
        std::vector<std::string> params;
        if (!only_topic.empty()) {
            sql += " AND topic = ?";
            params.push_back(only_topic);
        }
        if (!include_super) sql += " AND superseded_at IS NULL";
        sql += " ORDER BY made_at DESC LIMIT " + std::to_string(limit);

        std::cout << "ID  TOPIC                          STANCE                          AGE     PIN\n";
        std::cout << std::string(85, '-') << "\n";
        std::time_t now = std::time(nullptr);
        int n = 0;
        db.query(sql, params, [&](const core::Row& r){
            if (r.size() < 10) return;
            ++n;
            int64_t made = 0;
            try { made = std::stoll(r[8]); } catch (...) {}
            long age_d = made ? (now - made) / 86400 : 0;
            std::string pin = r[5] == "1" ? "[PIN]" : "";
            if (!r[7].empty() && r[7] != "0") pin = "[SUPER]";
            std::cout << r[0] << "  "
                      << r[1].substr(0, 30) << "  "
                      << r[2].substr(0, 30) << "  "
                      << age_d << "d  "
                      << pin << "\n";
        });
        if (n == 0) std::cout << "  (none)\n";
        return 0;
    }

    int cmdCheck(const std::vector<std::string>& args) {
        if (args.empty()) {
            std::cerr << "icmg drift check: requires <prompt-text>\n";
            return 1;
        }
        std::string prompt;
        for (auto& a : args) {
            if (!a.empty() && a[0] == '-') continue;
            if (!prompt.empty()) prompt.push_back(' ');
            prompt += a;
        }
        std::string lp = lower(prompt);

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        std::vector<std::string> hits;
        db.query(
            "SELECT id, topic, stance, keywords FROM decisions "
            "WHERE pinned = 1 AND superseded_at IS NULL",
            {}, [&](const core::Row& r){
                if (r.size() < 4) return;
                std::string kw_lower = lower(r[3]);
                std::string topic_lower = lower(r[1]);
                bool match = false;
                // Match: any keyword (comma-sep) or topic appears in prompt.
                if (!topic_lower.empty() && lp.find(topic_lower) != std::string::npos)
                    match = true;
                else if (!kw_lower.empty()) {
                    std::stringstream ss(kw_lower);
                    std::string tok;
                    while (std::getline(ss, tok, ',')) {
                        // trim
                        size_t s = tok.find_first_not_of(" \t");
                        size_t e = tok.find_last_not_of(" \t");
                        if (s == std::string::npos) continue;
                        std::string t = tok.substr(s, e - s + 1);
                        if (!t.empty() && lp.find(t) != std::string::npos) {
                            match = true; break;
                        }
                    }
                }
                if (match) {
                    hits.push_back("id=" + r[0] + " topic=\"" + r[1]
                                   + "\" stance=\"" + r[2] + "\"");
                }
            });
        if (hits.empty()) return 0;
        std::cout << "[icmg drift] prompt touches " << hits.size()
                  << " pinned decision(s):\n";
        for (auto& h : hits) std::cout << "  - " << h << "\n";
        std::cout << "Verify your direction aligns. Override with `icmg drift supersede`.\n";
        return 1;  // non-zero so hook can inject warning
    }

    int cmdSupersede(const std::vector<std::string>& args) {
        if (args.size() < 1 || args[0][0] == '-') {
            std::cerr << "icmg drift supersede: <new-id> --over <old-id>\n";
            return 1;
        }
        int new_id = 0, old_id = 0;
        try { new_id = std::stoi(args[0]); } catch (...) { return 1; }
        try { old_id = std::stoi(flagValue(args, "--over")); } catch (...) {}
        if (old_id == 0) { std::cerr << "icmg drift supersede: --over required\n"; return 1; }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        db.run("UPDATE decisions SET supersedes = ?, superseded_at = strftime('%s','now') "
               "WHERE id = ?",
               {std::to_string(old_id), std::to_string(new_id)});
        db.run("UPDATE decisions SET superseded_at = strftime('%s','now') WHERE id = ?",
               {std::to_string(old_id)});
        std::cout << "icmg drift: " << new_id << " supersedes " << old_id << "\n";
        return 0;
    }

    int cmdReport(const std::vector<std::string>& args) {
        int days = 30;
        try { days = std::stoi(flagValue(args, "--days", "30")); } catch (...) {}
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        int64_t cutoff = std::time(nullptr) - (int64_t)days * 86400;

        // Topics with multiple non-superseded decisions (= flip).
        std::cout << "icmg drift report (last " << days << "d)\n"
                  << std::string(60, '-') << "\n";
        int flips = 0;
        db.query(
            "SELECT topic, COUNT(*) c FROM decisions "
            "WHERE made_at >= ? GROUP BY topic HAVING c > 1",
            {std::to_string(cutoff)}, [&](const core::Row& r){
                if (r.size() < 2) return;
                ++flips;
                std::cout << "  [flip] " << r[0] << "  (" << r[1] << " decisions)\n";
            });
        // Superseded counts.
        int super = 0;
        db.query("SELECT COUNT(*) FROM decisions WHERE superseded_at >= ?",
                 {std::to_string(cutoff)}, [&](const core::Row& r){
                     if (!r.empty()) super = std::stoi(r[0]);
                 });
        std::cout << "Total flips: " << flips << "  superseded: " << super << "\n";
        if (flips > 5) std::cout << "WARN: high flip rate; review pinned anchors.\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("drift", DriftCommand);

} // namespace icmg::cli

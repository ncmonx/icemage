// v1.21.7 (FB2): `icmg transcript` — full-text search captured session
// transcripts via FTS5.
//
// Subcommands:
//   icmg transcript search "<query>" [--limit N] [--session SID]
//   icmg transcript list [--limit N]      list recent recordings
//   icmg transcript stats                 row count + bytes stored
//   icmg transcript show <id>             dump one entry verbatim
//   icmg transcript prune --older 90d     delete entries older than N days
//
// Backed by `transcripts` table + `transcripts_fts` virtual (mig 0034).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

namespace {

std::string excerpt(const std::string& text, size_t max_chars = 200) {
    if (text.size() <= max_chars) return text;
    return text.substr(0, max_chars) + "...";
}

std::string formatTs(int64_t epoch) {
    std::time_t t = (std::time_t)epoch;
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&t));
    return std::string(buf);
}

}  // namespace

class TranscriptCommand : public BaseCommand {
public:
    std::string name()        const override { return "transcript"; }
    std::string description() const override {
        return "FTS5 search captured session transcripts (v1.21.7 FB2)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg transcript <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  search \"<query>\" [--limit N] [--session SID]\n"
            "      Full-text search (FTS5) across recorded transcripts.\n"
            "  list [--limit N]\n"
            "      Recent recordings (default 20).\n"
            "  stats\n"
            "      Row count + total bytes stored.\n"
            "  show <id>\n"
            "      Dump one entry verbatim.\n"
            "  prune --older Nd\n"
            "      Delete entries older than N days.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h") || args.empty()) {
            usage(); return 0;
        }
        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        core::Db db(core::Config::instance().projectDbPath("."));

        if (sub == "search") {
            if (rest.empty()) {
                std::cerr << "transcript search: <query> required\n"; return 1;
            }
            std::string q = rest[0];
            std::string limit_s = flagValue(rest, "--limit");
            int limit = 20;
            if (!limit_s.empty()) {
                try { limit = std::stoi(limit_s); } catch (...) {}
            }
            std::string session = flagValue(rest, "--session");

            std::string sql =
                "SELECT t.id, t.session_id, t.recorded_at, "
                "       snippet(transcripts_fts, 0, '[', ']', '...', 16) "
                "FROM transcripts_fts "
                "JOIN transcripts t ON t.id = transcripts_fts.rowid "
                "WHERE transcripts_fts MATCH ?";
            std::vector<std::string> binds = {q};
            if (!session.empty()) {
                sql += " AND t.session_id = ?";
                binds.push_back(session);
            }
            sql += " ORDER BY rank LIMIT ?";
            binds.push_back(std::to_string(limit));

            int found = 0;
            db.query(sql, binds, [&](const core::Row& r){
                if (r.size() < 4) return;
                ++found;
                int64_t ts = 0;
                try { ts = std::stoll(r[2]); } catch (...) {}
                std::cout << "#" << r[0] << "  [" << formatTs(ts)
                          << "]  session=" << r[1] << "\n  "
                          << r[3] << "\n\n";
            });
            if (found == 0) {
                std::cout << "transcript search: 0 matches for \"" << q << "\"\n";
            }
            return 0;
        }

        if (sub == "list") {
            std::string limit_s = flagValue(rest, "--limit");
            int limit = 20;
            if (!limit_s.empty()) {
                try { limit = std::stoi(limit_s); } catch (...) {}
            }
            int n = 0;
            db.query(
                "SELECT id, session_id, char_len, recorded_at "
                "FROM transcripts ORDER BY recorded_at DESC LIMIT ?",
                {std::to_string(limit)},
                [&](const core::Row& r){
                    if (r.size() < 4) return;
                    ++n;
                    int64_t ts = 0;
                    try { ts = std::stoll(r[3]); } catch (...) {}
                    std::cout << "#" << std::setw(5) << r[0] << "  "
                              << formatTs(ts) << "  "
                              << std::setw(8) << r[2] << " chars  session="
                              << r[1] << "\n";
                });
            if (n == 0) std::cout << "transcript list: no recordings yet\n";
            return 0;
        }

        if (sub == "stats") {
            int total = 0;
            int64_t bytes = 0;
            db.query("SELECT COUNT(*), COALESCE(SUM(char_len),0) FROM transcripts",
                     {}, [&](const core::Row& r){
                         if (r.size() < 2) return;
                         try {
                             total = std::stoi(r[0]);
                             bytes = std::stoll(r[1]);
                         } catch (...) {}
                     });
            std::cout << "=== Transcript stats ===\n"
                      << "  recordings: " << total << "\n"
                      << "  total chars: " << bytes
                      << " (" << (bytes / 1024) << " KiB)\n";
            // top-5 sessions
            std::cout << "\nTop sessions by recording count:\n";
            db.query(
                "SELECT session_id, COUNT(*) FROM transcripts "
                "GROUP BY session_id ORDER BY COUNT(*) DESC LIMIT 5", {},
                [&](const core::Row& r){
                    if (r.size() < 2) return;
                    std::cout << "  " << std::setw(40) << std::left << r[0]
                              << std::setw(0) << "  " << r[1] << "\n";
                });
            return 0;
        }

        if (sub == "show") {
            if (rest.empty()) {
                std::cerr << "transcript show: <id> required\n"; return 1;
            }
            int found = 0;
            db.query(
                "SELECT session_id, recorded_at, char_len, content "
                "FROM transcripts WHERE id = ?", {rest[0]},
                [&](const core::Row& r){
                    if (r.size() < 4) return;
                    ++found;
                    int64_t ts = 0;
                    try { ts = std::stoll(r[1]); } catch (...) {}
                    std::cout << "=== Transcript #" << rest[0] << " ===\n"
                              << "  session:  " << r[0] << "\n"
                              << "  recorded: " << formatTs(ts) << "\n"
                              << "  chars:    " << r[2] << "\n\n"
                              << r[3] << "\n";
                });
            if (found == 0) {
                std::cerr << "transcript show: id " << rest[0] << " not found\n";
                return 1;
            }
            return 0;
        }

        if (sub == "prune") {
            std::string older = flagValue(rest, "--older");
            if (older.empty()) {
                std::cerr << "transcript prune: --older Nd required\n"; return 1;
            }
            // strip trailing 'd'
            if (!older.empty() && (older.back() == 'd' || older.back() == 'D')) {
                older.pop_back();
            }
            int days = 0;
            try { days = std::stoi(older); } catch (...) {}
            if (days <= 0) {
                std::cerr << "transcript prune: bad --older value\n"; return 1;
            }
            int64_t cutoff = (int64_t)std::time(nullptr) - (int64_t)days * 86400;
            int before = 0;
            db.query("SELECT COUNT(*) FROM transcripts WHERE recorded_at < ?",
                     {std::to_string(cutoff)},
                     [&](const core::Row& r){
                         if (!r.empty()) { try { before = std::stoi(r[0]); }
                                           catch (...) {} }
                     });
            db.run("DELETE FROM transcripts WHERE recorded_at < ?",
                   {std::to_string(cutoff)});
            std::cout << "transcript prune: deleted " << before
                      << " entries older than " << days << " days\n";
            return 0;
        }

        std::cerr << "transcript: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("transcript", TranscriptCommand);

}  // namespace icmg::cli

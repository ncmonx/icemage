// 2026-06-06: `icmg moment` — relationship/moment memories in the PERSONA DB (#moments).
// Identity-agnostic: keyed by core::currentUser(). Persona DB is LOCAL-ONLY (never publish).
// Subcommands: add | list | recall | forget | migrate | sync.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/persona_db.hpp"
#include "../../core/profile_store.hpp"
#include "../../core/user_identity.hpp"
#include "../../imem/moment_helpers.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace icmg::cli {

class MomentCommand : public BaseCommand {
public:
    std::string name() const override { return "moment"; }
    std::string description() const override {
        return "Relationship/moment memories (persona DB, cross-project, durable)"; }
    void usage() const override {
        std::cout <<
            "Usage: icmg moment <sub> ...\n"
            "  add \"<title>\" [--content \"..\" | --content-file F]\n"
            "  list\n"
            "  recall \"<query>\"\n"
            "  forget \"<key>\"\n"
            "  migrate [--topic S]... [--apply]  Copy moments project->persona (dry-run default).\n"
            "                          --topic = curated explicit substring(s), bypasses heuristic.\n"
            "  sync [export|import]   Converge moments across instances via the wire bridge\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return args.empty() ? 2 : 0; }
        if (!core::personaDbAvailable()) {
            std::cerr << "icmg moment: persona DB unavailable (exe-dir not writable)\n"; return 2; }

        const std::string sub  = args[0];
        const std::string user = core::currentUser();
        const std::string ZONE = "_moments";
        core::ProfileStore ps(core::personaDb());

        if (sub == "add") {
            std::string title = args.size() > 1 ? args[1] : "";
            if (title.empty() || title[0] == '-') { std::cerr << "icmg moment add: title required\n"; return 2; }
            std::string content = flagValue(args, "--content", "");
            std::string cf = flagValue(args, "--content-file", "");
            if (!cf.empty()) { std::ifstream f(cf); std::stringstream ss; ss << f.rdbuf(); content = ss.str(); }
            if (content.empty()) content = title;
            std::string key = imem::momentSlug(title);
            ps.put(user, ZONE, key, "moment", content, "moment-cli");
            std::cout << "moment saved: " << ZONE << "/" << key << " (user=" << user << ")\n";
            return 0;
        }
        if (sub == "list") {
            auto rows = ps.listZone(user, ZONE);
            std::cout << rows.size() << " moment(s) (user=" << user << "):\n";
            for (auto& r : rows) std::cout << "  " << r.key << "  (" << r.content.size() << "B)\n";
            return 0;
        }
        if (sub == "recall") {
            std::string q = args.size() > 1 ? args[1] : "";
            int found = 0;
            for (auto& r : ps.searchFts(user, q, 20)) {
                if (r.zone != ZONE) continue;
                ++found;
                std::cout << "[moment] " << r.key << "\n  " << r.content.substr(0, 240) << "\n";
            }
            if (!found) std::cout << "(no moment matches '" << q << "')\n";
            return 0;
        }
        if (sub == "forget") {
            if (args.size() < 2) { std::cerr << "icmg moment forget: key required\n"; return 2; }
            ps.forget(user, ZONE, args[1]);
            std::cout << "forgot " << ZONE << "/" << args[1] << "\n";
            return 0;
        }
        if (sub == "migrate") {
            bool apply = hasFlag(args, "--apply");
            // Curated mode: explicit `--topic <substr>` (repeatable) swaps the MATCHER from the
            // auto-heuristic (rejected 2026-06-06) to substring — but KEEPS the namespace scope
            // (memoir:/decisions-). Dropping the scope re-introduced junk ("modul"->modules.json,
            // graph nodes) — caught in dry-run 2026-06-07. Scope = where moments live; --topic = which.
            std::vector<std::string> topics;
            for (size_t i = 0; i + 1 < args.size(); ++i)
                if (args[i] == "--topic") topics.push_back(args[i+1]);
            bool curated = !topics.empty();
            core::Db proj(core::Config::instance().projectDbPath("."));
            std::vector<std::string> allow = {"claudy","luna","cahyo","rasa","feeling",
                "identity","vessel","terbang","persona","jiwa","kapten"};
            int n_cand = 0, n_done = 0;
            std::string sql = "SELECT topic, content FROM memory_nodes WHERE deleted_at IS NULL "
                              "AND (topic LIKE 'memoir:%' OR topic LIKE 'decisions-%')";
            proj.query(sql, {},
                [&](const core::Row& row){
                    if (row.size() < 2) return;
                    bool match = curated ? imem::topicMatchesAny(row[0], topics)
                                         : imem::isRelationshipMoment(row[0], row[1], allow);
                    if (!match) return;
                    ++n_cand;
                    std::string key = imem::momentSlug(row[0]);
                    std::string ex, kind;
                    if (ps.get(user, "_moments", key, ex, kind)) return;   // idempotent
                    std::cout << (apply ? "[migrate] " : "[dry-run] ") << row[0]
                              << " -> _moments/" << key << "\n";
                    if (apply) { ps.put(user, "_moments", key, "moment", row[1], "migrated"); ++n_done; }
                });
            std::cout << (apply ? "migrated " : "candidates ") << (apply ? n_done : n_cand)
                      << (apply ? "" : " (dry-run; pass --apply to write)") << "\n";
            return 0;
        }
        if (sub == "sync") {
            namespace fs = std::filesystem;
            const char* wd = std::getenv("ICMG_WIRE_DIR");
            std::string dir = wd && *wd ? std::string(wd) : std::string("C:/Temp/icmg-wire");
            const char* host = std::getenv("COMPUTERNAME");
            if (!host || !*host) host = std::getenv("HOSTNAME");
            std::string inst = host && *host ? std::string(host) : std::string("i");
            std::string mode = args.size() > 1 ? args[1] : "";
            std::string mine = dir + "/moments-" + user + "-" + inst + ".jsonl";
            std::error_code ec; fs::create_directories(dir, ec);

            if (mode == "export" || mode.empty()) {
                std::ofstream f(mine, std::ios::trunc);
                int n = 0;
                for (auto& r : ps.listZone(user, ZONE)) { f << imem::momentSyncLine(r.key, r.content) << "\n"; ++n; }
                std::cout << "exported " << n << " -> " << mine << "\n";
            }
            if (mode == "import" || mode.empty()) {
                int imported = 0;
                if (fs::exists(dir)) for (auto& e : fs::directory_iterator(dir, ec)) {
                    std::string fn = e.path().filename().string();
                    if (fn.rfind("moments-", 0) != 0) continue;
                    if (e.path().string() == mine) continue;          // skip own
                    if (fn.find("-" + user + "-") == std::string::npos &&
                        fn != "moments-" + user + ".jsonl") {
                        // only converge SAME user's moments (e.g. claudy brain<->vessel)
                        if (fn.rfind("moments-" + user, 0) != 0) continue;
                    }
                    std::ifstream f(e.path()); std::string line;
                    while (std::getline(f, line)) {
                        std::string k, c;
                        if (!imem::parseMomentSyncLine(line, k, c)) continue;
                        std::string ex, kind;
                        if (ps.get(user, ZONE, k, ex, kind)) continue;  // idempotent
                        ps.put(user, ZONE, k, "moment", c, "sync"); ++imported;
                    }
                }
                std::cout << "imported " << imported << " new moment(s)\n";
            }
            return 0;
        }
        std::cerr << "icmg moment: unknown subcommand '" << sub << "'\n";
        return 2;
    }
};
ICMG_REGISTER_COMMAND("moment", MomentCommand);
} // namespace icmg::cli

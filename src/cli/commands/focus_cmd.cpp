// `icmg focus` — persistent session-scoped todo list with per-prompt re-injection.
//
// Subcommands:
//   icmg focus add "<todo text>"     — append todo; session from ICMG_SESSION_ID (default "default")
//   icmg focus done <id>             — mark item done
//   icmg focus block <id>            — mark item blocked
//   icmg focus unblock <id>          — mark item in-progress
//   icmg focus list [--json] [--all] — list items; --all shows all sessions
//   icmg focus clear                 — wipe current session todos
//   icmg focus inject                — emit markdown block for hook re-injection
//                                      (honours ICMG_FOCUS_QUIET=1 → empty)

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/focus_chain.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

class FocusCommand : public BaseCommand {
public:
    std::string name()        const override { return "focus"; }
    std::string description() const override {
        return "Persistent session todo list with per-prompt hook re-injection";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg focus <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  add \"<text>\"        Append a todo for the current session\n"
            "  done <id>           Mark todo done\n"
            "  block <id>          Mark todo blocked\n"
            "  unblock <id>        Mark todo back to in-progress\n"
            "  list [--json][--all] List todos (default: current session, in-progress)\n"
            "  clear               Delete all todos for the current session\n"
            "  inject              Emit markdown block (used by hook; quiet if ICMG_FOCUS_QUIET=1)\n\n"
            "Environment:\n"
            "  ICMG_SESSION_ID     Session identifier (default: \"default\")\n"
            "  ICMG_FOCUS_QUIET=1  Suppress inject output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage();
            return args.empty() ? 1 : 0;
        }

        const std::string& sub = args[0];

        // Reject unknown subcommands before opening DB.
        static const char* kKnownSubs[] = {
            "add", "done", "block", "unblock", "list", "clear", "inject", nullptr
        };
        bool known = false;
        for (int i = 0; kKnownSubs[i]; ++i) {
            if (sub == kKnownSubs[i]) { known = true; break; }
        }
        if (!known) {
            std::cerr << "icmg focus: unknown subcommand '" << sub << "'\n";
            std::cerr << "Run `icmg focus --help` for usage.\n";
            return 1;
        }

        // Resolve session id.
        std::string session_id = "default";
        const char* env_sid = std::getenv("ICMG_SESSION_ID");
        if (env_sid && env_sid[0] != '\0') session_id = env_sid;

        // Open project DB.
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::FocusChain fc(db);

        // ---- add --------------------------------------------------------
        if (sub == "add") {
            if (args.size() < 2) {
                std::cerr << "icmg focus add: missing todo text\n";
                return 1;
            }
            std::string text = args[1];
            int64_t id = fc.add(session_id, text);
            std::cout << "focus: added #" << id << ": " << text << "\n";
            return 0;
        }

        // ---- done / block / unblock -------------------------------------
        if (sub == "done" || sub == "block" || sub == "unblock") {
            if (args.size() < 2) {
                std::cerr << "icmg focus " << sub << ": missing id\n";
                return 1;
            }
            int64_t id = 0;
            try { id = std::stoll(args[1]); } catch (...) {
                std::cerr << "icmg focus " << sub << ": invalid id: " << args[1] << "\n";
                return 1;
            }
            std::string new_status = (sub == "done")    ? "done"
                                   : (sub == "block")   ? "blocked"
                                   :                      "in";
            if (!fc.setStatus(id, new_status)) {
                std::cerr << "icmg focus " << sub << ": failed (invalid id or status)\n";
                return 1;
            }
            std::cout << "focus: #" << id << " → " << new_status << "\n";
            return 0;
        }

        // ---- list -------------------------------------------------------
        if (sub == "list") {
            bool json_mode = hasFlag(args, "--json");
            bool all_mode  = hasFlag(args, "--all");

            std::vector<imem::FocusItem> items;
            if (all_mode) {
                // Fetch all sessions: query directly.
                db.query(
                    "SELECT id, session_id, todo, status, ord FROM focus_chain ORDER BY session_id, ord",
                    {},
                    [&](const core::Row& row) {
                        imem::FocusItem item;
                        if (!row[0].empty()) try { item.id = std::stoll(row[0]); } catch (...) {}
                        item.session_id = row.size() > 1 ? row[1] : "";
                        item.todo       = row.size() > 2 ? row[2] : "";
                        item.status     = row.size() > 3 ? row[3] : "";
                        if (row.size() > 4 && !row[4].empty()) {
                            try { item.ord = std::stoi(row[4]); } catch (...) {}
                        }
                        items.push_back(item);
                    }
                );
            } else {
                items = fc.list(session_id, "in");
            }

            if (json_mode) {
                std::cout << "[";
                for (size_t i = 0; i < items.size(); ++i) {
                    auto& it = items[i];
                    if (i > 0) std::cout << ",";
                    // Minimal JSON escaping for todo text.
                    std::string escaped;
                    for (char c : it.todo) {
                        if (c == '"')  escaped += "\\\"";
                        else if (c == '\\') escaped += "\\\\";
                        else if (c == '\n') escaped += "\\n";
                        else escaped += c;
                    }
                    std::cout << "{\"id\":" << it.id
                              << ",\"session_id\":\"" << it.session_id << "\""
                              << ",\"todo\":\"" << escaped << "\""
                              << ",\"status\":\"" << it.status << "\""
                              << ",\"ord\":" << it.ord << "}";
                }
                std::cout << "]\n";
            } else {
                if (items.empty()) {
                    std::cout << "(no todos)\n";
                } else {
                    for (auto& it : items) {
                        std::string box = (it.status == "done")    ? "[x]"
                                        : (it.status == "blocked") ? "[!]"
                                        :                            "[ ]";
                        std::cout << "#" << it.id << " " << box
                                  << " [" << it.session_id << "] " << it.todo << "\n";
                    }
                }
            }
            return 0;
        }

        // ---- clear ------------------------------------------------------
        if (sub == "clear") {
            fc.removeBySession(session_id);
            std::cout << "focus: cleared session " << session_id << "\n";
            return 0;
        }

        // ---- inject -----------------------------------------------------
        if (sub == "inject") {
            // Honour opt-out env.
            const char* quiet = std::getenv("ICMG_FOCUS_QUIET");
            if (quiet && std::string(quiet) == "1") return 0;

            auto items = fc.list(session_id, "in", 5);
            if (items.empty()) return 0;

            std::ostringstream out;
            out << "## Focus chain (current session todos)\n";
            for (auto& it : items) {
                std::string box = (it.status == "done") ? "[x]" : "[ ]";
                out << "- " << box << " " << it.todo << "\n";
            }
            std::cout << out.str();
            return 0;
        }

        // Should never reach here (all known subcommands handled above).
        return 1;
    }
};

ICMG_REGISTER_COMMAND("focus", FocusCommand);

} // namespace icmg::cli

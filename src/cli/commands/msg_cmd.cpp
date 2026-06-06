// `icmg msg` — me-everywhere #4: cross-session messaging.
//
// `msg send <to|*> "<body>"` appends a directed/broadcast message to a shared
// append-only log; `msg inbox [--since <ts>]` reads messages addressed to me (or
// broadcast) newer than the cursor and reports the newest ts for polling.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/msg.hpp"
#include "../../core/path_utils.hpp"   // icmgGlobalDir
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <ctime>
#include <cstdlib>
#ifdef _WIN32
#  include <process.h>
#  define ICMG_GETPID _getpid
#else
#  include <unistd.h>
#  define ICMG_GETPID getpid
#endif

namespace icmg::cli {
namespace fs = std::filesystem;

class MsgCommand : public BaseCommand {
    static std::string file() { return core::wireDir() + "/msg.tsv"; }
    static std::string mySession() {
        const char* s = std::getenv("ICMG_SESSION_ID");
        if (s && *s) return s;
        return "pid-" + std::to_string((long)ICMG_GETPID());
    }
    static std::vector<core::Message> readAll() {
        std::vector<core::Message> v;
        std::ifstream f(file());
        std::string line;
        while (std::getline(f, line)) { core::Message m; if (core::msgFromLine(line, m)) v.push_back(m); }
        return v;
    }

public:
    std::string name()        const override { return "msg"; }
    std::string description() const override { return "Cross-session messaging between live sessions (send/inbox)"; }
    void usage() const override {
        std::cout << "Usage: icmg msg send <to|*> \"<body>\" | inbox [--since <ts>]\n"
                  << "  send    send to a session id, or * to broadcast\n"
                  << "  inbox   read messages addressed to you (or *) newer than --since\n";
    }

    int run(const std::vector<std::string>& args) override {
        const std::string sub = args.empty() ? "" : args[0];
        const int64_t now = (int64_t)std::time(nullptr);
        const std::string me = mySession();

        if (sub == "send") {
            if (args.size() < 3) { std::cerr << "icmg msg send: need <to> and <body>\n"; return 1; }
            std::string to = args[1];
            std::string body;
            for (size_t i = 2; i < args.size(); ++i) { if (i > 2) body += ' '; body += args[i]; }
            core::Message m; m.ts = now; m.from = me; m.to = to; m.body = body;
            std::error_code ec; fs::create_directories(core::wireDir(), ec);
            { std::ofstream f(file(), std::ios::app); f << core::msgToLine(m) << "\n"; }
            auto all = readAll();
            if (all.size() > 1000) {   // opportunistic compaction: keep last 500
                std::ofstream w(file(), std::ios::trunc);
                for (size_t i = all.size() - 500; i < all.size(); ++i) w << core::msgToLine(all[i]) << "\n";
            }
            std::cout << "sent to " << to << "\n";
            return 0;
        }

        if (sub == "inbox") {
            int64_t since = 0;
            for (size_t i = 1; i + 1 < args.size(); ++i)
                if (args[i] == "--since") { try { since = std::stoll(args[i + 1]); } catch (...) {} }
            auto inbox = core::inboxSince(readAll(), me, since);
            int64_t newest = since;
            for (const auto& m : inbox) {
                if (m.ts > newest) newest = m.ts;
                std::cout << "  [" << (now - m.ts) << "s] " << m.from
                          << (m.to == "*" ? " (all)" : "") << ": " << m.body << "\n";
            }
            if (inbox.empty()) std::cout << "(no new messages)\n";
            std::cout << "cursor: " << newest << "\n";
            return 0;
        }

        usage();
        return sub.empty() ? 0 : 1;
    }
};

ICMG_REGISTER_COMMAND("msg", MsgCommand);

}  // namespace icmg::cli
